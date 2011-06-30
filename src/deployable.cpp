/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Authors: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of pacemaker-cloud.
 *
 * pacemaker-cloud is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * pacemaker-cloud is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pacemaker-cloud.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"

#include <qb/qblog.h>
#include <uuid/uuid.h>

#include <libxml/parser.h>
#include <libxslt/transform.h>

#include "pcmk_pe.h"

#include <string>
#include <map>

#include "mainloop.h"
#include "config_loader.h"
#include "deployable.h"
#include "assembly.h"
#include "resource.h"

using namespace std;

bool
Deployable::process_qmf_events(void)
{
	uint32_t rc = 0;
	ConsoleEvent event;
	Assembly *a;
	map<string, Assembly*>::iterator iter;

	for (iter = _assemblies.begin(); iter != _assemblies.end(); iter++) {
		a = iter->second;
		if (a->state_get() != Assembly::STATE_ONLINE) {
			a->matahari_discover(session);
		}
	}
	a = NULL;
	while (session->nextEvent(event, qpid::messaging::Duration::IMMEDIATE)) {
		a = _agents_ass[event.getAgent().getName()];
		if (a) {
			a->process_qmf_events(event);
		}
		if (event.getType() == CONSOLE_AGENT_DEL) {
			_agents_ass.erase(event.getAgent().getName());
		}
	}
	return true;
}

void
Deployable::map_agents_ass(string &agent_name, Assembly *a)
{
	_agents_ass[agent_name] = a;
}

static void
_poll_for_qmf_events(gpointer data)
{
	Deployable *d = (Deployable *)data;
	qb_loop_timer_handle timer_handle;

	if (d->process_qmf_events()) {
		mainloop_timer_add(1000, data,
				   _poll_for_qmf_events,
				   &timer_handle);
	}
}

Deployable::Deployable(std::string& uuid, CommonAgent *agent) :
	_name(""), _uuid(uuid), _crmd_uuid(""), _config(NULL), _pe(NULL),
	_status_changed(false), _agent(agent), _file_count(0),
	_resource_counter(0)
{
	qb_loop_timer_handle timer_handle;
	std::stringstream filter;
	string url("localhost:49000");
	uuid_t tmp_id;
	char tmp_id_s[37];

	xmlInitParser();

	uuid_generate(tmp_id);
	uuid_unparse(tmp_id, tmp_id_s);
	_crmd_uuid.insert(0, (char*)tmp_id_s, sizeof(tmp_id_s));

	reload();

	connection = new qpid::messaging::Connection(url, "");
	connection->open();

	session = new ConsoleSession(*connection, "max-agent-age:1");

	filter << "[and";
	filter << ", [eq, _vendor, [quote, 'matahariproject.org']]";
//	filter << ", [eq, _product, [quote, 'service']]";
//	filter << ", [or";
//	for (iter = _assemblies.begin(); iter != _assemblies.end(); iter++) {
//		filter << ", [eq, hostname, [quote, '" << iter->second->name_get() << "']]";
//	}
//	filter << "]";
	filter << "]";

	session->setAgentFilter(filter.str());

	session->open();

	qb_log(LOG_INFO, "session open. Filter is: %s", filter.str().c_str());

	mainloop_timer_add(1000, this,
			   _poll_for_qmf_events,
			   &timer_handle);
}

Deployable::~Deployable()
{
	map<string, Assembly*>::iterator kill;
	map<string, Assembly*>::iterator iter;
	Assembly *a;

	for (iter = _assemblies.begin(); iter != _assemblies.end(); ) {
		kill = iter;
		a = kill->second;
		iter++;
		_assemblies.erase(kill);
		a->stop();
	}
	session->close();
	connection->close();

	/* Shutdown libxml */
	xmlCleanupParser();
}

void
Deployable::create_services(string& ass_name, xmlNode * services)
{
	xmlNode *cur_node = NULL;
	string name;
	string type;

	for (cur_node = services; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		type = (char*)xmlGetProp(cur_node, BAD_CAST "name");
		name = "rsc_";
		name += ass_name;
		name += "_";
		name += type;
		qb_log(LOG_DEBUG, "service name: %s", name.c_str());

		if (_resources[name] == NULL) {
			string cl = "lsb";
			string pr = "pacemaker";
			_resources[name] = new Resource(this, name, type,
							cl, pr);
		}
	}
}

void
Deployable::create_assemblies(xmlNode * assemblies)
{
	string ass_name;
	string ass_uuid;
	xmlNode *cur_node = NULL;
	xmlNode *child_node = NULL;

	for (cur_node = assemblies; cur_node; cur_node = cur_node->next) {
		if (cur_node->type != XML_ELEMENT_NODE) {
			continue;
		}
		ass_name = (char*)xmlGetProp(cur_node, BAD_CAST "name");
		ass_uuid = (char*)xmlGetProp(cur_node, BAD_CAST "uuid");
		qb_log(LOG_DEBUG, "node name: %s", ass_name.c_str());

		for (child_node = cur_node->children; child_node; child_node = child_node->next) {
			if (child_node->type != XML_ELEMENT_NODE) {
				continue;
			}
			if (strcmp((char*)child_node->name, "services") == 0) {
				create_services(ass_name, child_node->children);
			}
		}
		assembly_add(ass_name, ass_uuid);
	}
}

void
Deployable::reload(void)
{
	int32_t rc;
	xmlNode *cur_node = NULL;
	xmlNode *dep_node = NULL;
	xsltStylesheetPtr ss = NULL;
	const char *params[1];
	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);

	if (_config != NULL) {
		xmlFreeDoc(_config);
		_config = NULL;
	}
	if (_pe != NULL) {
		xmlFreeDoc(_pe);
		_pe = NULL;
	}

	rc = config_get(_uuid, &_config);
	if (rc != 0) {
		qb_log(LOG_ERR, "unable to load XML config file");
		// O, crap
		// try again later?
		_config = NULL;
		return;
	}
	_resource_counter = 1;

	ss = xsltParseStylesheetFile(BAD_CAST "/usr/share/pacemaker-cloud/cf2pe.xsl");
	params[0] = NULL;
	_pe = xsltApplyStylesheet(ss, _config, params);

	dep_node = xmlDocGetRootElement(_config);
	for (cur_node = dep_node->children; cur_node;
	     cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			if (strcmp((char*)cur_node->name, "assemblies") == 0) {
				create_assemblies(cur_node->children);
			}
		}
	}

	xsltFreeStylesheet(ss);
}

static void
_status_timeout(void *data)
{
	Deployable *d = (Deployable *)data;

	if (pe_is_busy_processing()) {
		// try later
		qb_log(LOG_INFO, "pe_is_busy_processing: trying later");
		d->schedule_processing();
	} else {
		d->process();
	}
}

static void
resource_execute_cb(struct pe_operation *op)
{
	Deployable *d = (Deployable *)op->user_data;
	d->resource_execute(op);
}

void
Deployable::resource_execute(struct pe_operation *op)
{
	Resource *r = resource_get(op);
	assert(r != NULL);

	if (op->interval > 0 && strcmp(op->method, "monitor") == 0) {
		r->start_recurring(op);
	} else if (strcmp(op->method, "delete") == 0) {
		r->delete_op_history(op);
	} else if (strcmp(op->method, "stop") == 0) {
		r->stop(op);
	} else {
		r->execute(op);
	}
}

static void
transition_completed_cb(void* user_data, int32_t result)
{
	Deployable *d = (Deployable *)user_data;
	d->transition_completed(result);
}

void
Deployable::transition_completed(int32_t result)
{
	qb_log(LOG_INFO, "-- transition_completed -- %d", result);
}

Resource*
Deployable::resource_get(struct pe_operation *op)
{
	string name = op->rname;
	return _resources[name];
}

Assembly*
Deployable::assembly_get(std::string& node_uuid)
{
	return _assemblies[node_uuid];
}

void
Deployable::process(void)
{
	xmlNode * cur_node = NULL;
	xmlNode * pe_root = NULL;
	xmlNode * status = NULL;
	xmlNode * rscs = NULL;
	int32_t rc = 0;
	Assembly *a;
	Resource *r;
	::qpid::sys::Mutex::ScopedLock _lock(xml_lock);

	if (_pe == NULL) {
		return;
	}
	_status_changed = true;

	if (_dc_uuid.length() == 0 ||
	    _assemblies[_dc_uuid]->state_get() != Assembly::STATE_ONLINE) {
		for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
		     a_iter != _assemblies.end(); a_iter++) {
			a = a_iter->second;
			if (a->state_get() == Assembly::STATE_ONLINE) {
				_dc_uuid = a->uuid_get();
			}
		}
	}

	pe_root = xmlDocGetRootElement(_pe);
	xmlSetProp(pe_root, BAD_CAST "dc-uuid", BAD_CAST dc_uuid_get().c_str());
	for (cur_node = pe_root->children; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE &&
		    strcmp((char*)cur_node->name, "status") == 0) {
			status = cur_node;
			break;
		}
	}
	if (status) {
		xmlUnlinkNode(status);
		xmlFreeNode(status);
	}
	status = xmlNewChild(pe_root, NULL, BAD_CAST "status", NULL);

	for (map<string, Assembly*>::iterator a_iter = _assemblies.begin();
	     a_iter != _assemblies.end(); a_iter++) {
		a = a_iter->second;
		a->insert_status(status);
	}

#if 0
	stringstream nf;
	nf << "pe-out-" << _file_count <<  ".xml";
	_file_count++;

	qb_log(LOG_INFO, "processing new state with %s", nf.str().c_str());
	xmlSaveFormatFileEnc(nf.str().c_str(), _pe, "UTF-8", 1);
#endif
	rc = pe_process_state(pe_root, resource_execute_cb,
			      transition_completed_cb, this);
	_status_changed = false;
	if (rc != 0) {
		schedule_processing();
	}
}

void
Deployable::schedule_processing(void)
{
	if (_status_changed) {
		qb_log(LOG_INFO, "not scheduling - collecting status");
		// busy collecting status
		return;
	}

	if (mainloop_timer_is_running(_processing_timer)) {
		qb_log(LOG_INFO, "not scheduling - already scheduled");
	} else {
		mainloop_timer_add(1000, this,
				   _status_timeout,
				   &_processing_timer);
	}
}

void
Deployable::service_state_changed(const string& ass_name, string& service_name,
				  string &state, string &reason)
{
	qmf::Data event = qmf::Data(_agent->package.event_service_state_change);

	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", ass_name);
	event.setProperty("service", service_name);
	event.setProperty("state", state);
	event.setProperty("reason", reason);
	_agent->agent_session.raiseEvent(event);
}

void
Deployable::assembly_state_changed(Assembly *a, string state, string reason)
{
	qb_loop_timer_handle th;
	qmf::Data event = qmf::Data(_agent->package.event_assembly_state_change);

	event.setProperty("deployable", _uuid);
	event.setProperty("assembly", a->name_get());
	event.setProperty("state", state);
	event.setProperty("reason", reason);
	_agent->agent_session.raiseEvent(event);
	schedule_processing();
}

int32_t
Deployable::assembly_add(string& name, string& uuid)
{
	Assembly *a = _assemblies[uuid];
	if (a) {
		// don't want duplicates
		return -1;
	}

	try {
		a = new Assembly(this, name, uuid);
	} catch (qpid::types::Exception e) {
		qb_log(LOG_ERR, "Exception creating Assembly %s",
		       e.what());
		delete a;
		return -1;
	}
	_assemblies[uuid] = a;
	return 0;
}

int32_t
Deployable::assembly_remove(string& uuid, string& name)
{
	Assembly *a = _assemblies[uuid];
	if (a) {
		_assemblies.erase(uuid);
		a->stop();
	}
	return 0;
}

