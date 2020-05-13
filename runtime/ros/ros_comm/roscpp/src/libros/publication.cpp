/*
 * Copyright (C) 2008, Morgan Quigley and Willow Garage, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Stanford University or Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ros/publication.h"
#include "ros/subscriber_link.h"
#include "ros/connection.h"
#include "ros/callback_queue_interface.h"
#include "ros/single_subscriber_publisher.h"
#include "ros/serialization.h"
#include <std_msgs/Header.h>
#include "ros/names.h"
#include "ros/config_comm.h" 
#include "ros/param.h" 

#include "ros/this_node.h"
#include "ros/topic_manager.h"
#include "modules/routing/proto/routing.pb.h"
#include "modules/control/proto/control_cmd.pb.h"
#include "modules/localization/proto/localization.pb.h"
#include "modules/planning/proto/planning.pb.h"
#include <gcrypt.h>

namespace ros
{

class PeerConnDisconnCallback : public CallbackInterface
{
public:
  PeerConnDisconnCallback(const SubscriberStatusCallback& callback, const SubscriberLinkPtr& sub_link, bool use_tracked_object, const VoidConstWPtr& tracked_object)
  : callback_(callback)
  , sub_link_(sub_link)
  , use_tracked_object_(use_tracked_object)
  , tracked_object_(tracked_object)
  {
  }

  virtual CallResult call()
  {
    VoidConstPtr tracker;
    if (use_tracked_object_)
    {
      tracker = tracked_object_.lock();

      if (!tracker)
      {
        return Invalid;
      }
    }

    SingleSubscriberPublisher pub(sub_link_);
    callback_(pub);

    return Success;
  }

private:
  SubscriberStatusCallback callback_;
  SubscriberLinkPtr sub_link_;
  bool use_tracked_object_;
  VoidConstWPtr tracked_object_;
};

Publication::Publication(const std::string &name,
                         const std::string &datatype,
                         const std::string &_md5sum,
                         const std::string& message_definition,
                         size_t max_queue,
                         bool latch,
                         bool has_header)
: name_(name),
  datatype_(datatype),
  md5sum_(_md5sum),
  message_definition_(message_definition),
  max_queue_(max_queue),
  seq_(0),
  dropped_(false),
  latch_(latch),
  has_header_(has_header),
  self_published_(false),
  intraprocess_subscriber_count_(0)
{
}

Publication::~Publication()
{
  drop();
}

void Publication::addCallbacks(const SubscriberCallbacksPtr& callbacks)
{
  boost::mutex::scoped_lock lock(callbacks_mutex_);

  callbacks_.push_back(callbacks);

  // Add connect callbacks for all current subscriptions if this publisher wants them
  if (callbacks->connect_ && callbacks->callback_queue_)
  {
    boost::mutex::scoped_lock lock(subscriber_links_mutex_);
    V_SubscriberLink::iterator it = subscriber_links_.begin();
    V_SubscriberLink::iterator end = subscriber_links_.end();
    for (; it != end; ++it)
    {
      const SubscriberLinkPtr& sub_link = *it;
      CallbackInterfacePtr cb(boost::make_shared<PeerConnDisconnCallback>(callbacks->connect_, sub_link, callbacks->has_tracked_object_, callbacks->tracked_object_));
      callbacks->callback_queue_->addCallback(cb, (uint64_t)callbacks.get());
    }
  }
}

void Publication::removeCallbacks(const SubscriberCallbacksPtr& callbacks)
{
  boost::mutex::scoped_lock lock(callbacks_mutex_);

  V_Callback::iterator it = std::find(callbacks_.begin(), callbacks_.end(), callbacks);
  if (it != callbacks_.end())
  {
    const SubscriberCallbacksPtr& cb = *it;
    if (cb->callback_queue_)
    {
      cb->callback_queue_->removeByID((uint64_t)cb.get());
    }
    callbacks_.erase(it);
  }
}

void Publication::drop()
{
  // grab a lock here, to ensure that no subscription callback will
  // be invoked after we return
  {
    boost::mutex::scoped_lock lock(publish_queue_mutex_);
    boost::mutex::scoped_lock lock2(subscriber_links_mutex_);

    if (dropped_)
    {
      return;
    }

    dropped_ = true;
  }

  dropAllConnections();
}

void Publication::updateMsgLen(char* bytes, int n) {
  bytes[3] = (n >> 24) & 0xFF;
  bytes[2] = (n >> 16) & 0xFF;
  bytes[1] = (n >> 8) & 0xFF;
  bytes[0] = n & 0xFF;
}

bool Publication::applyPlanningSubPolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  if (subp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::planning::ADCTrajectory r;
  if (r.ParseFromString(str)) {
      bool unchange = true;
      for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "total_path_length" && r.has_total_path_length())
               r.clear_total_path_length();
           else if ((*subp)[k] == "total_path_time" && r.has_total_path_time())
               r.clear_total_path_time();
           else if ((*subp)[k] == "latency_stats" && r.has_latency_stats())
               r.clear_latency_stats();
           else if ((*subp)[k] == "is_replan" && r.has_is_replan())
               r.clear_is_replan();
           else if ((*subp)[k] == "right_of_way_status" && r.has_right_of_way_status())
               r.clear_right_of_way_status();
           else if ((*subp)[k] == "lane_id" && r.lane_id_size())
               r.clear_lane_id();
           else if ((*subp)[k] == "estop" && r.has_estop())
               r.clear_estop();
           else if ((*subp)[k] == "routing_header" && r.has_routing_header())
               r.clear_routing_header();
           else if ((*subp)[k] == "debug" && r.has_debug())
               r.clear_debug();
           else if ((*subp)[k] == "gear" && r.has_gear())
               r.clear_gear();
           else if ((*subp)[k] == "decision" && r.has_decision())
               r.clear_decision();
           else if ((*subp)[k] == "path_point" && r.path_point_size())
               r.clear_path_point();
           else if ((*subp)[k] == "trajectory_point" && r.trajectory_point_size())
               r.clear_trajectory_point();
           else
               unclear = true;
           unchange &= unclear;
      }
      if (unchange)
          return true;
      if (r.SerializeToString(&str)) {
          unsigned int n = str.length() + 8;
          if (n != m.num_bytes) {
              m.num_bytes = n;
              updateMsgLen((char*)m.buf.get(), n-4);
              updateMsgLen((char*)(m.buf.get()+4), n-8);
          }
          memcpy(m.buf.get()+8, str.c_str(), str.length());
          return true;
      }
      return false;
  }
  return false;
}


bool Publication::applyPlanningPolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* pubp = TopicManager::instance()->get_pubop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  std::vector<std::string>* cubp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), "xx");
  int i1 = (pubp == NULL? 1 : 0);
  int i2 = (subp == NULL? 1 : 0);
  int i3 = (cubp == NULL? 1 : 0);
  ROS_INFO("Publication::enqueueMsg planning result %d %d %d", i1, i2, i3);
  if (pubp == NULL && subp == NULL && cubp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::planning::ADCTrajectory r;
  int pos = 0;
  char buffer[10000];
  unsigned int n = 8;
  if (pubp != NULL || subp != NULL) {
    if (r.ParseFromString(str)) {
       bool unchange = true;
       if (subp != NULL) {
         for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "total_path_length" && r.has_total_path_length())
               r.clear_total_path_length();
           else if ((*subp)[k] == "total_path_time" && r.has_total_path_time())
               r.clear_total_path_time();
           else if ((*subp)[k] == "latency_stats" && r.has_latency_stats())
               r.clear_latency_stats();
           else if ((*subp)[k] == "is_replan" && r.has_is_replan())
               r.clear_is_replan();
           else if ((*subp)[k] == "right_of_way_status" && r.has_right_of_way_status())
               r.clear_right_of_way_status();
           else if ((*subp)[k] == "lane_id" && r.lane_id_size())
               r.clear_lane_id();
           else if ((*subp)[k] == "estop" && r.has_estop())
               r.clear_estop();
           else if ((*subp)[k] == "routing_header" && r.has_routing_header())
               r.clear_routing_header();
           else if ((*subp)[k] == "debug" && r.has_debug())
               r.clear_debug();
           else if ((*subp)[k] == "gear" && r.has_gear())
               r.clear_gear();
           else if ((*subp)[k] == "decision" && r.has_decision())
               r.clear_decision();
           else if ((*subp)[k] == "path_point" && r.path_point_size())
               r.clear_path_point();
           else if ((*subp)[k] == "trajectory_point" && r.trajectory_point_size())
               r.clear_trajectory_point();
           else
               unclear = true;
           unchange &= unclear;
         }
         ROS_INFO("Publication::enqueueMsg apply sub policy");
         if (!unchange) {
             if (r.SerializeToString(&str)) {
                  memcpy(m.buf.get()+n, str.c_str(), str.length());
                  //n += str.length();
                  ROS_INFO("Publication::enqueueMsg serialized %d", str.length());
             }
             else {
                  ROS_INFO("Publication::enqueueMsg serialize fail");
                  return false;
             }
         }
       }
      }
      else {
          return false;
      }
   }
   n += str.length();
   if (cubp != NULL) {
      std::string str = TopicManager::instance()->pop_hash();
      memcpy(m.buf.get()+n, str.c_str(), str.length());
      pos += str.length();
      n += str.length();
   }
   if (n != m.num_bytes) {
       m.num_bytes = n;
       updateMsgLen((char*)m.buf.get(), n-4);
       updateMsgLen((char*)(m.buf.get()+4), n-8-pos);
   }
   return true;
}

bool Publication::applyLocalizationEstimateSubPolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  if (subp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::localization::LocalizationEstimate r;
  if (r.ParseFromString(str)) {
      bool unchange = true;
      for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "uncertainty" && r.has_uncertainty())
               r.clear_uncertainty();
           else if ((*subp)[k] == "measurement_time" && r.has_measurement_time())
               r.clear_measurement_time();
             else if ((*subp)[k] == "header" && r.has_header())
               r.clear_header();
             else if ((*subp)[k] == "pose" && r.has_pose())
               r.clear_pose();
           else
               unclear = true;
           unchange &= unclear;
      }
      if (unchange)
          return true;
      if (r.SerializeToString(&str)) {
          unsigned int n = str.length() + 8;
          if (n != m.num_bytes) {
              m.num_bytes = n;
              updateMsgLen((char*)m.buf.get(), n-4);
              updateMsgLen((char*)(m.buf.get()+4), n-8);
          }
          memcpy(m.buf.get()+8, str.c_str(), str.length());
          return true;
      }
      return false;
  }
  return false;
}

bool Publication::applyLocalizationEstimatePolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* pubp = TopicManager::instance()->get_pubop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  std::vector<std::string>* cubp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), "xx");
  int i1 = (pubp == NULL? 1 : 0);
  int i2 = (subp == NULL? 1 : 0);
  int i3 = (cubp == NULL? 1 : 0);
  ROS_INFO("Publication::enqueueMsg localization result %d %d %d", i1, i2, i3);
  if (pubp == NULL && subp == NULL && cubp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::localization::LocalizationEstimate r;
  int pos = 0;
  char buffer[10000];
  unsigned int n = 8;              
  if (pubp != NULL || subp != NULL) {
      if (r.ParseFromString(str)) {
          bool unchange = true;
          if (subp != NULL) {
            for (int k = 0; k < subp->size(); k++) {
               bool unclear = false;
               if ((*subp)[k] == "uncertainty" && r.has_uncertainty())
                   r.clear_uncertainty();
               else if ((*subp)[k] == "measurement_time" && r.has_measurement_time())
                   r.clear_measurement_time();
               else if ((*subp)[k] == "header" && r.has_header())
                   r.clear_header();
               else if ((*subp)[k] == "pose" && r.has_pose())
                   r.clear_pose();
               else
                   unclear = true;
               unchange &= unclear;
             }
             ROS_INFO("Publication::enqueueMsg apply sub policy");
             if (!unchange) {
                if (r.SerializeToString(&str)) {
                    memcpy(m.buf.get()+n, str.c_str(), str.length());
                    //n += str.length();
                    ROS_INFO("Publication::enqueueMsg serialized %d", str.length());
                }
                else {
                    ROS_INFO("Publication::enqueueMsg serialize fail");
                    return false;
                }
             }
          }
          n += str.length();
          if (pubp != NULL) {
             int buf_len = 10000;
             for (int k = 0; k < pubp->size(); k++) {
                  if ((*pubp)[k] == "xx") {
                     std::string ss = "helloworld_localization";
                     int len = sign(ss, buffer+pos, buf_len-pos); //str, buffer+pos, buf_len-pos);
                     ROS_INFO("Publication::enqueueMsg signature %d", len);
/*                     if (len > 0)
                         pos += len;
  */                }
             }
             ROS_INFO("Publication::enqueueMsg apply pub policy");
 // Don't write to msg
/*
             if (pos > 0) {
                 memcpy(m.buf.get()+n, buffer, pos);
                 n += pos;
             }
 */         }
      }
      else {
          return false;
      }
   }
   if (cubp != NULL) {
      std::string str = TopicManager::instance()->pop_hash();   
      memcpy(m.buf.get()+n, str.c_str(), str.length());
      pos += str.length();
      n += str.length();
      ROS_INFO("Publication::enqueueMsg forward hash");
   }
   if (n != m.num_bytes) {
       m.num_bytes = n;
       updateMsgLen((char*)m.buf.get(), n-4);
       updateMsgLen((char*)(m.buf.get()+4), n-8-pos);
   }
   return true;
}

bool Publication::applyControlCommandSubPolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  if (subp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::control::ControlCommand r;
  if (r.ParseFromString(str)) {
      bool unchange = true;
      for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "speed" && r.has_speed())
               r.clear_speed();
           else if ((*subp)[k] == "acceleration" && r.has_acceleration())
               r.clear_acceleration();
           else if ((*subp)[k] == "engine_on_off" && r.has_engine_on_off())
               r.clear_engine_on_off();
           else if ((*subp)[k] == "trajectory_fraction" && r.has_trajectory_fraction())
               r.clear_trajectory_fraction();
           else if ((*subp)[k] == "debug" && r.has_debug())
               r.clear_debug();
           else if ((*subp)[k] == "latency_stats" && r.has_latency_stats())
               r.clear_latency_stats();
           else if ((*subp)[k] == "throttle" && r.has_throttle())
               r.clear_throttle();
           else if ((*subp)[k] == "brake" && r.has_brake())
               r.clear_brake();
           else if ((*subp)[k] == "steering_rate" && r.has_steering_rate())
               r.clear_steering_rate();
           else if ((*subp)[k] == "steering_target" && r.has_steering_target())
               r.clear_steering_target();
           else if ((*subp)[k] == "parking_brake" && r.has_parking_brake())
               r.clear_parking_brake();
           else if ((*subp)[k] == "gear_location" && r.has_gear_location())
               r.clear_gear_location();
           else if ((*subp)[k] == "signal" && r.has_signal())
               r.clear_signal();
           else if ((*subp)[k] == "pad_msg" && r.has_pad_msg())
               r.clear_pad_msg();
           else if ((*subp)[k] == "header" && r.has_header())
               r.clear_header();
           else
               unclear = true;
           unchange &= unclear;
      }
      if (unchange)
          return true;
      if (r.SerializeToString(&str)) {
          unsigned int n = str.length() + 8;
          if (n != m.num_bytes) {
              m.num_bytes = n;
              updateMsgLen((char*)m.buf.get(), n-4);
              updateMsgLen((char*)(m.buf.get()+4), n-8);
          }
          memcpy(m.buf.get()+8, str.c_str(), str.length());
          return true;
      }
      return false;
  }
  return false;
}

int Publication::sign(std::string str, char* buf, int maxlen) {
  ROS_INFO("Publication::enqueueMsg sign %d", str.length());
  GcrySexp sig, sign_parms;
  size_t errof = 0;
  int rc;
  GcrySexp* skey = TopicManager::instance()->get_privk();
  rc = gcry_sexp_build(&sign_parms, &errof, "%s", str.c_str());
  if (rc == 0) { 
      rc = gcry_pk_sign(&sig, sign_parms, *skey);
      if (rc == 0)
          return gcry_sexp_sprint(sig, GCRYSEXP_FMT_DEFAULT, buf, maxlen);
  }
  return -1;
}

bool Publication::applyRoutingResponsePolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) {
  std::vector<std::string>* pubp = TopicManager::instance()->get_pubop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  if (pubp == NULL && subp == NULL)
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
  apollo::routing::RoutingResponse r;
  if (r.ParseFromString(str)) {
      bool unchange = true;
      if (subp != NULL) {
         for (int k = 0; k < subp->size(); k++) {
             bool unclear = false;
             if ((*subp)[k] == "status" && r.has_status())
               r.clear_status();
             else if ((*subp)[k] == "measurement" && r.has_measurement())
               r.clear_measurement();
             else if ((*subp)[k] == "map_version" && r.has_map_version())
               r.clear_map_version();
             else if ((*subp)[k] == "routing_request" && r.has_routing_request())
               r.clear_routing_request();
             else if ((*subp)[k] == "header" && r.has_header())
               r.clear_header();
             else if ((*subp)[k] == "road" && r.road_size())
               r.clear_road();
             else
               unclear = true;
             unchange &= unclear;
         }
         ROS_INFO("Publication::enqueueMsg apply sub policy");
      }
      int pos = 0;
      char buffer[10000];
      if (pubp != NULL) {
         int buf_len = 10000;
         for (int k = 0; k < pubp->size(); k++) {
             if ((*pubp)[k] == "map_version" && r.has_map_version()) {
               int len = sign(r.map_version(), buffer+pos, buf_len-pos);
               ROS_INFO("Publication::enqueueMsg signature %d", len);
               if (len > 0)
                   pos += len;
             }
         }
         ROS_INFO("Publication::enqueueMsg apply pub policy");
      }
      if (unchange && pos == 0) 
          return true;
      unsigned int n = 8;
      if (!unchange) {
          if (r.SerializeToString(&str)) {
              memcpy(m.buf.get()+n, str.c_str(), str.length());
              n += str.length(); 
              ROS_INFO("Publication::enqueueMsg serialized %d", str.length());
          }
          else {
             ROS_INFO("Publication::enqueueMsg serialize fail");
             return false;
          }
      }
      if (pos > 0) {
          memcpy(m.buf.get()+n, buffer, pos);
          n += pos;
      }
      if (n != m.num_bytes) {
          m.num_bytes = n;
          updateMsgLen((char*)m.buf.get(), n-4);
          updateMsgLen((char*)(m.buf.get()+4), n-8-pos);
      }
      return true;
  }
  return false;
}

bool Publication::applyRoutingResponseSubPolicy(const SubscriberLinkPtr& sub_link, SerializedMessage& m) { 
  std::vector<std::string>* subp = TopicManager::instance()->get_subop_policy(sub_link->getTopic(), sub_link->getDestinationCallerID());
  if (subp == NULL) 
      return false;
  std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
/*  
  bool unchange = true;
      for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "status")
               str[2] = str[2];
           else if ((*subp)[k] == "measurement")
               str[3] = str[3];
           else if ((*subp)[k] == "map_version")
               str[4] = str[4];
           else if ((*subp)[k] == "routing_request")
               str[5] = str[5];
           else if ((*subp)[k] == "header")
               str[6] = str[6];
           else if ((*subp)[k] == "road")
               str[7] = str[7];
           else
               unclear = true;
           unchange &= unclear;
      }
   if (unchange)
       return true;
   unsigned int n = str.length() + 8;
   m.num_bytes = n;
   updateMsgLen((char*)m.buf.get(), n-4);
   updateMsgLen((char*)(m.buf.get()+4), n-8);
   memcpy(m.buf.get()+8, str.c_str(), str.length());
   return true;
*/
  apollo::routing::RoutingResponse r;
  if (r.ParseFromString(str)) {
      bool unchange = true;
      for (int k = 0; k < subp->size(); k++) {
           bool unclear = false;
           if ((*subp)[k] == "status" && r.has_status())
               r.clear_status();
           else if ((*subp)[k] == "measurement" && r.has_measurement())
               r.clear_measurement();
           else if ((*subp)[k] == "map_version" && r.has_map_version()) 
               r.clear_map_version();
           else if ((*subp)[k] == "routing_request" && r.has_routing_request())
               r.clear_routing_request();
           else if ((*subp)[k] == "header" && r.has_header())
               r.clear_header();
           else if ((*subp)[k] == "road" && r.road_size())
               r.clear_road();
           else
               unclear = true;
           unchange &= unclear;
      }
      if (unchange)
          return true;
      if (r.SerializeToString(&str)) { 
          unsigned int n = str.length() + 8;
          if (n != m.num_bytes) {
              m.num_bytes = n;
              updateMsgLen((char*)m.buf.get(), n-4);
              updateMsgLen((char*)(m.buf.get()+4), n-8);
          }
          memcpy(m.buf.get()+8, str.c_str(), str.length());
          return true;
      }
      return false;
  }
  return false;

}

bool Publication::enqueueMessage(SerializedMessage& m)
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);
  if (dropped_)
  {
    return false;
  }

  ROS_ASSERT(m.buf);

  uint32_t seq = incrementSequence();
  if (has_header_)
  {
    // If we have a header, we know it's immediately after the message length
    // Deserialize it, write the sequence, and then serialize it again.
    namespace ser = ros::serialization;
    std_msgs::Header header;
    ser::IStream istream(m.buf.get() + 4, m.num_bytes - 4);
    ser::deserialize(istream, header);
    header.seq = seq;
    ser::OStream ostream(m.buf.get() + 4, m.num_bytes - 4);
    ser::serialize(ostream, header);
  }

  std::string topic_name = getName();
/*
  if (topic_name == "/apollo/routing_response" && 
       getName() != "/rosout" && this_node::getName().find("/play_") == std::string::npos) {
       //char* buffer = (char*) malloc(4*m.num_bytes+1);
       //sprintf(buffer, "%u %u %u %u", *(m.buf.get()), *(m.buf.get()+1), *(m.buf.get()+2), *(m.buf.get()+3));
       //for (int i = 4; i < m.num_bytes; i++)
       //     sprintf(buffer+strlen(buffer), " %u", *(m.buf.get()+i));
      std::string str((const char*)(m.buf.get()+8), m.num_bytes-8);
      apollo::routing::RoutingResponse r;
      if (r.ParseFromString(str)) {
          if (r.has_map_version()) {
              r.clear_map_version();  //set_map_version("2.500000");
              if (r.SerializeToString(&str)) {
                  unsigned int n = str.length() + 8;
                  if (n != m.num_bytes) {
                      m.num_bytes = n;
                      n -= 4;
                      char* bytes = (char*)m.buf.get();
                      bytes[3] = (n >> 24) & 0xFF;
                      bytes[2] = (n >> 16) & 0xFF;
                      bytes[1] = (n >> 8) & 0xFF;
                      bytes[0] = n & 0xFF;
                      n -= 4;
                      bytes[7] = (n >> 24) & 0xFF;
                      bytes[6] = (n >> 16) & 0xFF;
                      bytes[5] = (n >> 8) & 0xFF;
                      bytes[4] = n & 0xFF;
                      ROS_INFO("Publication::enqueueMessage serialize on on topic %s by %s %u %u %u %u %u %u %u %u", getName().c_str(), this_node::getName().c_str(), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
                  }
                  memcpy(m.buf.get()+8, str.c_str(), str.length());
              } 
          }
      }
      ROS_INFO("Publication::enqueueMessage on topic %s by %s %u seq %u len %u msg %u header %u", getName().c_str(), this_node::getName().c_str(), (uint32_t)subscriber_links_.size(), seq, m.num_bytes, str.length(), (unsigned int)has_header_);
       //free(buffer);
  }
*/ 
 
  if (topic_name != "/rosout" && this_node::getName().find("/play_") == std::string::npos) 
     ROS_INFO("Publication::enqueueMessage on topic %s by %s", getName().c_str(), this_node::getName().c_str());
     //ROS_INFO("Publication::enqueueMessage on topic %s by %s %u seq %u len %u msg %u header %u", getName().c_str(), this_node::getName().c_str(), (uint32_t)subscriber_links_.size(), seq, m.num_bytes,  (unsigned int)has_header_);
 
  for(V_SubscriberLink::iterator i = subscriber_links_.begin();
      i != subscriber_links_.end(); ++i)
  {
    const SubscriberLinkPtr& sub_link = (*i);
  /*
    if (topic_name != "/rosout" && this_node::getName().find("/play_") == std::string::npos) {
        bool policy_enforced = false;
        if (topic_name == "/apollo/routing_response") {
            ROS_INFO("Publication::enqueueMessage on topic %s msg %u seq %u bytes %u %u %u %u %u %u %u %u", topic_name.c_str(), m.num_bytes, seq, *(m.buf.get()), *(m.buf.get()+1), *(m.buf.get()+2), *(m.buf.get()+3), *(m.buf.get()+4), *(m.buf.get()+5), *(m.buf.get()+6), *(m.buf.get()+7));
            policy_enforced = applyRoutingResponsePolicy(sub_link, m);
        }
        else if (topic_name == "/apollo/control") {
            ROS_INFO("Publication::enqueueMessage on topic %s msg %u seq %u", topic_name.c_str(), m.num_bytes, seq);
         //   policy_enforced = applyControlCommandSubPolicy(sub_link, m);
        }
        else if (topic_name == "/apollo/planning") {
            ROS_INFO("Publication::enqueueMessage on topic %s msg %u seq %u", topic_name.c_str(), m.num_bytes, seq);
            policy_enforced = applyPlanningSubPolicy(sub_link, m);
        }
        else if (topic_name == "/apollo/localization/pose") {
            ROS_INFO("Publication::enqueueMessage on topic %s msg %u seq %u", topic_name.c_str(), m.num_bytes, seq);
            if (sub_link->getDestinationCallerID() == "/planning") 
               policy_enforced = applyLocalizationEstimatePolicy(sub_link, m);
            else if (sub_link->getDestinationCallerID() == "/control")
               policy_enforced = applyLocalizationEstimateSubPolicy(sub_link, m);        
        }
        if (policy_enforced)
            ROS_INFO("Publication::enqueueMessage on topic %s by %s to %s with policy msg %u", sub_link->getTopic().c_str(), this_node::getName().c_str(), sub_link->getDestinationCallerID().c_str(), m.num_bytes);
        else
            ROS_INFO("Publication::enqueueMessage on topic %s by %s to %s without policy msg %u", sub_link->getTopic().c_str(), this_node::getName().c_str(), sub_link->getDestinationCallerID().c_str(), m.num_bytes);
    }
  */
    if(sub_link->getDefaultTransport())
	{
      sub_link->enqueueMessage(m, true, false);      
    }

  }

  if (latch_)
  {
    last_message_ = m;
  }

  return true;
}

void Publication::setSelfPublished(bool self_published)
{
  self_published_ = self_published;
}

bool Publication::getSelfPublished()
{
  return self_published_;
}

TransportType Publication::getSubscriberlinksTransport()
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);

  uint32_t count = 0 ;
  bool intraprocess = false;
  TransportType ret = SHARED_MEMORY ;
  for (uint32_t i = 0; i< (uint32_t)subscriber_links_.size(); i++ )
  {
    if (subscriber_links_[i]->getDefaultTransport())
    {
      count ++ ;
    }

    if (subscriber_links_[i]->isIntraprocess()) 
    {
      intraprocess = true;
    }
  }

  if (intraprocess || latch_) {
    for (uint32_t i = 0; i < (uint32_t)subscriber_links_.size(); i++) 
    {
      subscriber_links_[i]->setDefaultTransport(true);
    }  
    ret = SOCKET;
    return ret;  
  }

  if (count == ((uint32_t)subscriber_links_.size()) && count != 0)
  {
    ret = SOCKET;
  }
  else if (count < ((uint32_t)subscriber_links_.size()) && count != 0)
  {
    ret = BOTH;
  }
  else
  {
    ret = SHARED_MEMORY;
  }

  return ret ;

}

extern struct ConfigComm g_config_comm;

void Publication::addSubscriberLink(const SubscriberLinkPtr& sub_link)
{
  {
    boost::mutex::scoped_lock lock(subscriber_links_mutex_);

    if (dropped_)
    {
      return;
    }

    subscriber_links_.push_back(sub_link);

    if (sub_link->isIntraprocess())
    {
      ++intraprocess_subscriber_count_;
    }
  }

  if (sub_link->getRospy())
  {
    ROS_DEBUG_STREAM("has rospy sub_link.");
    sub_link->setDefaultTransport(true);
  }
  else if (sub_link->isIntraprocess())
  {
    sub_link->setDefaultTransport(true);
    ROS_DEBUG_STREAM("Publication self_published:" << getName());
  }
  else if (latch_)
  {
    sub_link->setDefaultTransport(true);
    ROS_DEBUG_STREAM("Publication topic latched:" << getName());
  }
  else if (!g_config_comm.transport_mode && 
    g_config_comm.topic_white_list.find(getName()) == g_config_comm.topic_white_list.end() )
  {
    std::string ip_env;
    if (get_environment_variable(ip_env, "ROS_IP"))
    {
      ROS_DEBUG( "ROS_IP:%s", ip_env.c_str());
      if (ip_env.size() == 0)
      {
        sub_link->setDefaultTransport(false);
        ROS_WARN("invalid ROS_IP (an empty string)"); 
      } 
      else 
      {
        std::string remote_host = sub_link->getConnection()->getRemoteIp();
        std::string::size_type remote_colon_pos = remote_host.find_first_of(":");
        std::string remote_ip = remote_host.substr(0,remote_colon_pos);
    
        std::string local_host = sub_link->getConnection()->getLocalIp();

        std::string::size_type local_colon_pos = local_host.find_first_of(":");
        std::string local_ip = local_host.substr(0,local_colon_pos);

        if (remote_ip == local_ip) 
        {
          ROS_DEBUG_STREAM("Local IP == Remote IP , share memory transport : " << getName()); 
          sub_link->setDefaultTransport(false);  
        } 
        else
        {
          ROS_WARN_STREAM("Local IP != Remote IP , socket transport : " << getName() ); 
          sub_link->setDefaultTransport(true);  
        }
      }
    } 
    else 
    {
      sub_link->setDefaultTransport(false);
    }
  } 
  else 
  {
    sub_link->setDefaultTransport(true);
  }


  if (latch_ && last_message_.buf)
  {
    if (sub_link->getDefaultTransport()) 
    {
      sub_link->enqueueMessage(last_message_, true, true);      
    }
  }
  
  // This call invokes the subscribe callback if there is one.
  // This must happen *after* the push_back above, in case the
  // callback uses publish().
  peerConnect(sub_link);
}

void Publication::removeSubscriberLink(const SubscriberLinkPtr& sub_link)
{
  SubscriberLinkPtr link;
  {
    boost::mutex::scoped_lock lock(subscriber_links_mutex_);

    if (dropped_)
    {
      return;
    }

    if (sub_link->isIntraprocess())
    {
      --intraprocess_subscriber_count_;
    }

    V_SubscriberLink::iterator it = std::find(subscriber_links_.begin(), subscriber_links_.end(), sub_link);
    if (it != subscriber_links_.end())
    {
      link = *it;
      subscriber_links_.erase(it);
    }
  }

  if (link)
  {
    peerDisconnect(link);
  }
}

XmlRpc::XmlRpcValue Publication::getStats()
{
  XmlRpc::XmlRpcValue stats;
  stats[0] = name_;
  XmlRpc::XmlRpcValue conn_data;
  conn_data.setSize(0); // force to be an array, even if it's empty

  boost::mutex::scoped_lock lock(subscriber_links_mutex_);

  uint32_t cidx = 0;
  for (V_SubscriberLink::iterator c = subscriber_links_.begin();
       c != subscriber_links_.end(); ++c, cidx++)
  {
    const SubscriberLink::Stats& s = (*c)->getStats();
    conn_data[cidx][0] = (*c)->getConnectionID();
    // todo: figure out what to do here... the bytes_sent will wrap around
    // on some flows within a reasonable amount of time. xmlrpc++ doesn't
    // seem to give me a nice way to do 64-bit ints, perhaps that's a
    // limitation of xml-rpc, not sure. alternatively we could send the number
    // of KB transmitted to gain a few order of magnitude.
    conn_data[cidx][1] = (int)s.bytes_sent_;
    conn_data[cidx][2] = (int)s.message_data_sent_;
    conn_data[cidx][3] = (int)s.messages_sent_;
    conn_data[cidx][4] = 0; // not sure what is meant by connected
  }

  stats[1] = conn_data;
  return stats;
}

// Publisher : [(connection_id, destination_caller_id, direction, transport, topic_name, connected, connection_info_string)*]
// e.g. [(2, '/listener', 'o', 'TCPROS', '/chatter', 1, 'TCPROS connection on port 55878 to [127.0.0.1:44273 on socket 7]')]
void Publication::getInfo(XmlRpc::XmlRpcValue& info)
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);

  for (V_SubscriberLink::iterator c = subscriber_links_.begin();
       c != subscriber_links_.end(); ++c)
  {
    XmlRpc::XmlRpcValue curr_info;
    curr_info[0] = (int)(*c)->getConnectionID();
    curr_info[1] = (*c)->getDestinationCallerID();
    curr_info[2] = "o";
    curr_info[3] = (*c)->getTransportType();
    curr_info[4] = name_;
    curr_info[5] = true; // For length compatibility with rospy
    curr_info[6] = (*c)->getTransportInfo();
    info[info.size()] = curr_info;
  }
}

void Publication::dropAllConnections()
{
  // Swap our publishers list with a local one so we can only lock for a short period of time, because a
  // side effect of our calling drop() on connections can be re-locking the publishers mutex
  V_SubscriberLink local_publishers;

  {
    boost::mutex::scoped_lock lock(subscriber_links_mutex_);

    local_publishers.swap(subscriber_links_);
  }

  for (V_SubscriberLink::iterator i = local_publishers.begin();
           i != local_publishers.end(); ++i)
  {
    (*i)->drop();
  }
}

void Publication::peerConnect(const SubscriberLinkPtr& sub_link)
{
  V_Callback::iterator it = callbacks_.begin();
  V_Callback::iterator end = callbacks_.end();
  for (; it != end; ++it)
  {
    const SubscriberCallbacksPtr& cbs = *it;
    if (cbs->connect_ && cbs->callback_queue_)
    {
      CallbackInterfacePtr cb(boost::make_shared<PeerConnDisconnCallback>(cbs->connect_, sub_link, cbs->has_tracked_object_, cbs->tracked_object_));
      cbs->callback_queue_->addCallback(cb, (uint64_t)cbs.get());
    }
  }
}

void Publication::peerDisconnect(const SubscriberLinkPtr& sub_link)
{
  V_Callback::iterator it = callbacks_.begin();
  V_Callback::iterator end = callbacks_.end();
  for (; it != end; ++it)
  {
    const SubscriberCallbacksPtr& cbs = *it;
    if (cbs->disconnect_ && cbs->callback_queue_)
    {
      CallbackInterfacePtr cb(boost::make_shared<PeerConnDisconnCallback>(cbs->disconnect_, sub_link, cbs->has_tracked_object_, cbs->tracked_object_));
      cbs->callback_queue_->addCallback(cb, (uint64_t)cbs.get());
    }
  }
}

size_t Publication::getNumCallbacks()
{
  boost::mutex::scoped_lock lock(callbacks_mutex_);
  return callbacks_.size();
}

uint32_t Publication::incrementSequence()
{
  boost::mutex::scoped_lock lock(seq_mutex_);
  uint32_t old_seq = seq_;
  ++seq_;

  return old_seq;
}

uint32_t Publication::getNumSubscribers()
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);
  return (uint32_t)subscriber_links_.size();
}

void Publication::getPublishTypes(bool& serialize, bool& nocopy, const std::type_info& ti)
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);
  V_SubscriberLink::const_iterator it = subscriber_links_.begin();
  V_SubscriberLink::const_iterator end = subscriber_links_.end();
  for (; it != end; ++it)
  {
    const SubscriberLinkPtr& sub = *it;
    bool s = false;
    bool n = false;
    sub->getPublishTypes(s, n, ti);
    serialize = serialize || s;
    nocopy = nocopy || n;

    if (serialize && nocopy)
    {
      break;
    }
  }
}

bool Publication::hasSubscribers()
{
  boost::mutex::scoped_lock lock(subscriber_links_mutex_);
  return !subscriber_links_.empty();
}

void Publication::publish(SerializedMessage& m)
{
  if (m.message)
  {
    boost::mutex::scoped_lock lock(subscriber_links_mutex_);
    V_SubscriberLink::const_iterator it = subscriber_links_.begin();
    V_SubscriberLink::const_iterator end = subscriber_links_.end();
    for (; it != end; ++it)
    {
      const SubscriberLinkPtr& sub = *it;
      if (sub->isIntraprocess())
      {
        sub->enqueueMessage(m, false, true);
      }
    }

    m.message.reset();
  }

  if (m.buf)
  {
    boost::mutex::scoped_lock lock(publish_queue_mutex_);
    publish_queue_.push_back(m);
  }
}

void Publication::processPublishQueue()
{
  V_SerializedMessage queue;
  {
    boost::mutex::scoped_lock lock(publish_queue_mutex_);

    if (dropped_)
    {
      return;
    }

    queue.insert(queue.end(), publish_queue_.begin(), publish_queue_.end());
    publish_queue_.clear();
  }

  if (queue.empty())
  {
    return;
  }

  V_SerializedMessage::iterator it = queue.begin();
  V_SerializedMessage::iterator end = queue.end();
  for (; it != end; ++it)
  {
    enqueueMessage(*it);
  }
}

bool Publication::validateHeader(const Header& header, std::string& error_msg)
{
  std::string md5sum, topic, client_callerid;
  if (!header.getValue("md5sum", md5sum)
   || !header.getValue("topic", topic)
   || !header.getValue("callerid", client_callerid))
  {
    std::string msg("Header from subscriber did not have the required elements: md5sum, topic, callerid");

    ROS_ERROR("%s", msg.c_str());
    error_msg = msg;

    return false;
  }

  // Check whether the topic has been deleted from
  // advertised_topics through a call to unadvertise(), which could
  // have happened while we were waiting for the subscriber to
  // provide the md5sum.
  if (isDropped())
  {
    std::string msg = std::string("received a tcpros connection for a nonexistent topic [") +
                topic + std::string("] from [" + client_callerid +"].");

    ROS_ERROR("%s", msg.c_str());
    error_msg = msg;

    return false;
  }

  if (getMD5Sum() != md5sum &&
      (md5sum != std::string("*") && getMD5Sum() != std::string("*")))
  {
    std::string datatype;
    header.getValue("type", datatype);

    std::string msg = std::string("Client [") + client_callerid + std::string("] wants topic ") + topic +
                      std::string(" to have datatype/md5sum [") + datatype + "/" + md5sum +
                      std::string("], but our version has [") + getDataType() + "/" + getMD5Sum() +
                      std::string("]. Dropping connection.");

    ROS_ERROR("%s", msg.c_str());
    error_msg = msg;

    return false;
  }

  return true;
}

} // namespace ros
