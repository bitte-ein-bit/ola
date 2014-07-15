/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Port.cpp
 * Base implementation of the Port class.
 * Copyright (C) 2005 Simon Newton
 *
 * Unfortunately this file contains a lot of code duplication.
 */

#include <string>
#include <vector>
#include "ola/Logging.h"
#include "ola/rdm/UIDSet.h"
#include "olad/Device.h"
#include "olad/Port.h"
#include "olad/PortBroker.h"

namespace ola {

using std::string;

/*
 * Create a new basic input port
 */
BasicInputPort::BasicInputPort(AbstractDevice *parent,
                               unsigned int port_id,
                               const PluginAdaptor *plugin_adaptor,
                               bool supports_rdm):
    m_port_id(port_id),
    m_priority(ola::dmx::SOURCE_PRIORITY_DEFAULT),
    m_priority_mode(PRIORITY_MODE_STATIC),
    m_port_string(""),
    m_universe(NULL),
    m_device(parent),
    m_plugin_adaptor(plugin_adaptor),
    m_supports_rdm(supports_rdm) {
}


bool BasicInputPort::SetUniverse(Universe *new_universe) {
  Universe *old_universe = GetUniverse();
  if (old_universe == new_universe)
    return true;

  if (PreSetUniverse(old_universe, new_universe)) {
    m_universe = new_universe;
    PostSetUniverse(old_universe, new_universe);
    return true;
  }
  return false;
}


string BasicInputPort::UniqueId() const {
  if (m_port_string.empty()) {
    std::ostringstream str;
    if (m_device)
      str << m_device->UniqueId() << "-I-" << m_port_id;
    m_port_string = str.str();
  }
  return m_port_string;
}



bool BasicInputPort::SetPriority(uint8_t priority) {
  if (priority > ola::dmx::SOURCE_PRIORITY_MAX)
    return false;

  m_priority = priority;
  return true;
}


/*
 * Called when there is new data for this port
 */
void BasicInputPort::DmxChanged() {
  if (GetUniverse()) {
    const DmxBuffer &buffer = ReadDMX();
    uint8_t priority = (PriorityCapability() == CAPABILITY_FULL &&
                        GetPriorityMode() == PRIORITY_MODE_INHERIT ?
                        InheritedPriority() :
                        GetPriority());
    m_dmx_source.UpdateData(buffer, *m_plugin_adaptor->WakeUpTime(), priority);
    GetUniverse()->PortDataChanged(this);
  }
}


/*
 * Handle an RDM Request on this port.
 * @param request the RDMRequest object, ownership is transferred to us
 */
void BasicInputPort::HandleRDMRequest(const ola::rdm::RDMRequest *request,
                                      ola::rdm::RDMCallback *callback) {
  if (m_universe) {
    m_plugin_adaptor->GetPortBroker()->SendRDMRequest(
        this,
        m_universe,
        request,
        callback);
  } else {
    std::vector<std::string> packets;
    callback->Run(ola::rdm::RDM_FAILED_TO_SEND, NULL, packets);
    delete request;
  }
}


/*
 * Trigger the RDM Discovery procedure for this universe
 */
void BasicInputPort::TriggerRDMDiscovery(
    ola::rdm::RDMDiscoveryCallback *on_complete,
    bool full) {
  if (m_universe) {
    m_universe->RunRDMDiscovery(on_complete, full);
  } else {
    ola::rdm::UIDSet uids;
    on_complete->Run(uids);
  }
}


/*
 * Create a new BasicOutputPort
 */
BasicOutputPort::BasicOutputPort(AbstractDevice *parent,
                                 unsigned int port_id,
                                 bool start_rdm_discovery_on_patch,
                                 bool supports_rdm):
    m_port_id(port_id),
    m_discover_on_patch(start_rdm_discovery_on_patch),
    m_priority(ola::dmx::SOURCE_PRIORITY_DEFAULT),
    m_priority_mode(PRIORITY_MODE_INHERIT),
    m_port_string(""),
    m_universe(NULL),
    m_device(parent),
    m_supports_rdm(supports_rdm) {
}


bool BasicOutputPort::SetUniverse(Universe *new_universe) {
  Universe *old_universe = GetUniverse();
  if (old_universe == new_universe)
    return true;

  if (PreSetUniverse(old_universe, new_universe)) {
    m_universe = new_universe;
    PostSetUniverse(old_universe, new_universe);
    if (m_discover_on_patch)
      RunIncrementalDiscovery(
          NewSingleCallback(this, &BasicOutputPort::UpdateUIDs));
    return true;
  }
  return false;
}


string BasicOutputPort::UniqueId() const {
  if (m_port_string.empty()) {
    std::ostringstream str;
    if (m_device)
      str << m_device->UniqueId() << "-O-" << m_port_id;
    m_port_string = str.str();
  }
  return m_port_string;
}


bool BasicOutputPort::SetPriority(uint8_t priority) {
  if (priority > ola::dmx::SOURCE_PRIORITY_MAX)
    return false;

  m_priority = priority;
  return true;
}


/*
 * Handle an RDMRequest, subclasses can implement this to support RDM
 */
void BasicOutputPort::SendRDMRequest(const ola::rdm::RDMRequest *request,
                                     ola::rdm::RDMCallback *callback) {
  // broadcasts go to every port
  std::vector<std::string> packets;
  if (request->DestinationUID().IsBroadcast()) {
    delete request;
    callback->Run(ola::rdm::RDM_WAS_BROADCAST, NULL, packets);
  } else {
    OLA_WARN << "In base HandleRDMRequest, something has gone wrong with RDM"
      << " request routing";
    delete request;
    callback->Run(ola::rdm::RDM_FAILED_TO_SEND, NULL, packets);
  }
}


/*
 * This is a noop for ports that don't support RDM
 */
void BasicOutputPort::RunFullDiscovery(
    ola::rdm::RDMDiscoveryCallback *on_complete) {
  ola::rdm::UIDSet uids;
  on_complete->Run(uids);
}


/*
 * This is a noop for ports that don't support RDM
 */
void BasicOutputPort::RunIncrementalDiscovery(
    ola::rdm::RDMDiscoveryCallback *on_complete) {
  ola::rdm::UIDSet uids;
  on_complete->Run(uids);
}


/**
 * Called when the discovery triggered by patching completes
 */
void BasicOutputPort::UpdateUIDs(const ola::rdm::UIDSet &uids) {
  Universe *universe = GetUniverse();
  if (universe)
    universe->NewUIDList(this, uids);
}


/*
 * This allows switching based on Port type.
 */
template<class PortClass>
bool IsInputPort() {
  return true;
}

template<>
bool IsInputPort<OutputPort>() {
  return false;
}

template<>
bool IsInputPort<InputPort>() {
  return true;
}
}  // namespace ola
