<?xml version="1.0" encoding="ISO-8859-1"?>
<!-- Author: Angus Salkeld -->
<!-- -->
<!-- This is to convert a deployable configuration from Aeolus into the -->
<!-- configuration needed by pacemaker -->
<!-- -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="xml" indent="yes" encoding="utf-8" omit-xml-declaration="no"/>
<xsl:template match="/deployable">
<cib admin_epoch="1" epoch="1" num_updates="1" have-quorum="1" dc-uuid="0" validate-with="pacemaker-1.2">
  <configuration>
    <crm_config>
      <cluster_property_set id="bootstrap-options">
          <!--
          <nvpair id="opt-no-probes"        name="enable-startup-probes" value="false"/>
          <nvpair id="opt-stop-rsc"         name="stop-all-resources"     value="true"/>
	  <nvpair id="opt-stop-orphans"     name="stop-orphan-resources"  value="true"/>
          <nvpair id="opt-honey"       name="default-resource-stickiness" value="0"/>
          <nvpair id="opt-cluster-delay"    name="cluster-delay"          value="5s"/>

          -->
          <nvpair id="opt-startup-fencing"  name="startup-fencing"        value="false"/>
          <nvpair id="opt-health-strategy"  name="node-health-strategy"   value="none"/>
          <nvpair id="opt-no-start-failure" name="start-failure-is-fatal" value="false"/>
          <nvpair id="opt-not-symmetric"    name="symmetric-cluster"      value="false"/>
          <nvpair id="opt-stonith-disabled" name="stonith-enabled"        value="false"/>
          <nvpair id="opt-no-quorum-policy" name="no-quorum-policy"       value="ignore"/>
        </cluster_property_set>
      </crm_config>
      <rsc_defaults>
        <meta_attributes id="opt1">
          <nvpair id="rsc-default-2" name="is-managed-default" value="true"/>
        </meta_attributes>
        <meta_attributes id="opt2">
          <nvpair id="rsc-default-3" name="multiple-active" value="stop_start"/>
        </meta_attributes>
      </rsc_defaults>
    <nodes>
<xsl:for-each select="assemblies/assembly">
      <node>
<xsl:attribute name="id"><xsl:value-of select="@uuid"/></xsl:attribute>
<xsl:attribute name="uname"><xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="type">normal</xsl:attribute>
      </node>
</xsl:for-each>
    </nodes>
    <resources>
<xsl:for-each select="assemblies/assembly">
<xsl:variable name="ass_name" select="@name"/>
<xsl:for-each select="services/service">
      <primitive>
<xsl:attribute name="id">rsc_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="class"><xsl:value-of select="@class"/></xsl:attribute>
<xsl:attribute name="type"><xsl:value-of select="@type"/></xsl:attribute>
<xsl:if test="@class='ocf'">
    <xsl:attribute name="provider"><xsl:value-of select="@provider"/></xsl:attribute>
<instance_attributes>
<xsl:attribute name="id">attrs_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:for-each select="parameters/parameter">
	<nvpair>
<xsl:attribute name="id">param_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="name"><xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="value"><xsl:value-of select="value"/></xsl:attribute>
	</nvpair>
</xsl:for-each>
</instance_attributes>
</xsl:if>
        <operations>
          <op>
<xsl:attribute name="id">monitor_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="name">monitor</xsl:attribute>
<!--<xsl:attribute name="requires">nothing</xsl:attribute>-->
<!--<xsl:attribute name="on-fail">restart</xsl:attribute>-->
<xsl:attribute name="interval"><xsl:value-of select="@monitor_interval"/></xsl:attribute>
          </op>
        </operations>
      </primitive>
<xsl:if test="configure_executable">
      <primitive>
<xsl:attribute name="id">cfg_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="class">ocf</xsl:attribute>
<xsl:attribute name="type">script_runner</xsl:attribute>
<xsl:attribute name="provider">pacemaker-cloud</xsl:attribute>

<instance_attributes>
<xsl:attribute name="id">attrs_<xsl:value-of select="@name"/></xsl:attribute>
	<nvpair>
<xsl:attribute name="id">param_url_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="name">executable_url</xsl:attribute>
<xsl:attribute name="value"><xsl:value-of select="configure_executable/@url"/></xsl:attribute>
	</nvpair>
<xsl:for-each select="parameters/parameter">
	<nvpair>
<xsl:attribute name="id">param_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="name"><xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="value"><xsl:value-of select="value"/></xsl:attribute>
	</nvpair>
</xsl:for-each>
</instance_attributes>

      </primitive>
</xsl:if>

</xsl:for-each>
</xsl:for-each>
    </resources>
    <constraints>
<xsl:for-each select="assemblies/assembly">
<xsl:variable name="ass_name" select="@name"/>
<xsl:for-each select="services/service">
      <rsc_location>
<xsl:attribute name="id">loc_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="rsc">rsc_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="score">INFINITY</xsl:attribute>
<xsl:attribute name="node"><xsl:value-of select="$ass_name"/></xsl:attribute>
      </rsc_location>

<xsl:if test="configure_executable">
      <rsc_order>
<xsl:attribute name="id">depend_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="first">cfg_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="then">rsc_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="score">INFINITY</xsl:attribute>
      </rsc_order>
      <rsc_location>
<xsl:attribute name="id">loc_cfg_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="rsc">cfg_<xsl:value-of select="$ass_name"/>_<xsl:value-of select="@name"/></xsl:attribute>
<xsl:attribute name="score">INFINITY</xsl:attribute>
<xsl:attribute name="node"><xsl:value-of select="$ass_name"/></xsl:attribute>
      </rsc_location>

</xsl:if>

</xsl:for-each>
</xsl:for-each>

<xsl:for-each select="constraints/service_dependancy">
      <rsc_order>
<xsl:attribute name="id">depend_<xsl:value-of select="@id"/></xsl:attribute>
<xsl:attribute name="first"><xsl:value-of select="@first"/></xsl:attribute>
<xsl:attribute name="then"><xsl:value-of select="@then"/></xsl:attribute>
<xsl:attribute name="score">INFINITY</xsl:attribute>
      </rsc_order>
</xsl:for-each>

    </constraints>
  </configuration>
</cib>
</xsl:template>
</xsl:stylesheet>
