/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * E133Node.cpp
 * Copyright (C) 2011 Simon Newton
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include HASH_MAP_H

#include <ola/Callback.h>
#include <ola/Logging.h>
#include <ola/network/InterfacePicker.h>

#include <string>

#include "plugins/e131/e131/CID.h"

#include "E133UniverseController.h"
#include "E133Node.h"

using std::string;


E133Node::E133Node(const string &preferred_ip,
                   uint16_t port)
    : m_preferred_ip(preferred_ip),
      m_timeout_event(NULL),
      m_cid(ola::plugin::e131::CID::Generate()),
      m_transport(port),
      m_root_layer(&m_transport, m_cid),
      m_e133_layer(&m_root_layer),
      m_dmp_inflator(&m_e133_layer) {
}


E133Node::~E133Node() {
  if (m_timeout_event)
    m_ss.RemoveTimeout(m_timeout_event);
}


bool E133Node::Init() {
  ola::network::Interface interface;
  const ola::network::InterfacePicker *picker =
    ola::network::InterfacePicker::NewPicker();
  if (!picker->ChooseInterface(&interface, m_preferred_ip)) {
    OLA_INFO << "Failed to find an interface";
    delete picker;
    return false;
  }
  delete picker;

  if (!m_transport.Init(interface)) {
    return false;
  }

  ola::network::UdpSocket *socket = m_transport.GetSocket();
  m_ss.AddSocket(socket);
  m_e133_layer.SetInflator(&m_dmp_inflator);

  m_timeout_event = m_ss.RegisterRepeatingTimeout(
      500,
      ola::NewCallback(this, &E133Node::CheckForStaleRequests));
  return true;
}


/**
 * Register a E133UniverseController
 * @param controller E133UniverseController to register
 * @return true if the registration succeeded, false otherwise.
 */
bool E133Node::RegisterController(E133UniverseController *controller) {
  controller_map::iterator iter = m_controller_map.find(
      controller->Universe());
  if (iter == m_controller_map.end()) {
    m_controller_map[controller->Universe()] = controller;
    controller->SetE133Layer(&m_e133_layer);
    m_dmp_inflator.SetRDMHandler(
        controller->Universe(),
        ola::NewCallback(controller, &E133UniverseController::HandleResponse));
    return true;
  }
  return false;
}


/**
 * Deregister a E133UniverseController
 * @param controller E133UniverseController to register
 */
void E133Node::DeRegisterController(E133UniverseController *controller) {
  controller_map::iterator iter = m_controller_map.find(
      controller->Universe());
  if (iter != m_controller_map.end()) {
    controller->SetE133Layer(NULL);
    m_dmp_inflator.RemoveRDMHandler(controller->Universe());
    // TODO(simon): timeout all existing requests at this point
    m_controller_map.erase(iter);
  }
}


bool E133Node::CheckForStaleRequests() {
  const ola::TimeStamp *now = m_ss.WakeUpTime();
  controller_map::iterator iter = m_controller_map.begin();
  for (; iter != m_controller_map.end(); ++iter) {
    OLA_INFO << "Checking";
    iter->second->CheckForStaleRequests(now);
  }
  return true;
}