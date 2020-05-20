/*
 * Copyright (c) 2018-2022 Hila Ben Abraham and Jyoti Parwatikar
 * and Washington University in St. Louis
*/
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/**
 * Derived from ChronoSync2013 by Jeff Thompson <jefft0@remap.ucla.edu>
 * Derived from ChronoChat-js by Qiuhan Ding and Wentao Shang.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version, with the additional exemption that
 * compiling, linking, and/or using OpenSSL is allowed.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * A copy of the GNU Lesser General Public License is in the file COPYING.
 */


#include <stdexcept>
#include <ndn-cxx/util/logger.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include "sync-state.pb.h"
//#include "../c/util/time.h"
#include <ndn-cxx/util/time.hpp>
#include <ndn-cxx/util/scheduler.hpp>
#include "ict-vector-state.hpp"
#include "ictsync.hpp"

NDN_LOG_INIT(ict.ICTSync);

using namespace std;
using namespace ndn;

namespace ict {
ICTSync::Impl::Impl
  (const OnReceivedSyncState& onReceivedSyncState,
   const OnInitialized& onInitialized, const Name& applicationDataPrefix,
   const Name& applicationBroadcastPrefix, int sessionNo, Face& face,
   KeyChain& keyChain, const Name& certificateName, time::milliseconds syncLifetime,
   int previousSequenceNumber, bool isDiscovery, bool noData, std::chrono::milliseconds syncUpdateInt)
: onReceivedSyncState_(onReceivedSyncState), onInitialized_(onInitialized),
  applicationDataPrefixUri_(applicationDataPrefix.toUri()),
  applicationBroadcastPrefix_(applicationBroadcastPrefix), sessionNo_(sessionNo),
  face_(face), keyChain_(keyChain), certificateName_(certificateName),
  syncLifetime_(syncLifetime), initialPreviousSequenceNo_(previousSequenceNumber),
  sequenceNo_(previousSequenceNumber), digestTree_(new ICTVectorState()),
  pendingInterests_(), enabled_(true), isDiscovery_(isDiscovery), noData_(noData),
  syncUpdateInterval_(syncUpdateInt)
{
  //lastInterestId_ = 0;
  nextInterestTs_ =  std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      if (!scheduler_) scheduler_ = unique_ptr<ndn::Scheduler>(new ndn::Scheduler(face_.getIoService()));
}


  //JP added to be called if register fails
void
ICTSync::Impl::reRegister(const  RegisterPrefixFailureCallback& onRegisterFailed)
{
  // Register the prefix with the face and use our own onInterest
  InterestFilter int_filter(applicationBroadcastPrefix_);
  broadcastPrefixRegId_ = face_.setInterestFilter(int_filter,
						 (InterestCallback)bind(&ICTSync::Impl::onInterest, shared_from_this(), _1, _2),
						 onRegisterFailed);

  NDN_LOG_DEBUG("reRegister prefix");
  NDN_LOG_DEBUG(applicationBroadcastPrefix_.toUri());
}

void
ICTSync::Impl::initialize(const RegisterPrefixFailureCallback& onRegisterFailed)
{
  Sync::SyncStateMsg emptyContent;
  InterestFilter int_filter(applicationBroadcastPrefix_);

  // Register the prefix with the face
  broadcastPrefixRegId_ = face_.setInterestFilter(int_filter,
						 (InterestCallback)bind(&ICTSync::Impl::onInterest, shared_from_this(), _1, _2),
						 onRegisterFailed);

  Name iname(applicationBroadcastPrefix_);
  iname.append("00");
  Interest interest(iname);
  //interest.getName().append("00");
  interest.setInterestLifetime(time::milliseconds(1000));
  //think we want the following - JP
  interest.setCanBePrefix(true);
  interest.setDefaultCanBePrefix(true);
  face_.expressInterest
    (interest, bind(&ICTSync::Impl::onData, shared_from_this(), _1, _2),
     bind(&ICTSync::Impl::initialNack, shared_from_this(), _1, _2),
     bind(&ICTSync::Impl::initialTimeout, shared_from_this(), _1));

  NDN_LOG_DEBUG("initial sync expressed");
  NDN_LOG_DEBUG(interest.getName().toUri());
  scheduler_->schedule(time::milliseconds(syncUpdateInterval_.count()), bind(&ICTSync::Impl::checkForUpdate, this));
}

void
ICTSync::Impl::shutdown()
{
  enabled_ = false;
  broadcastPrefixRegId_.unregister();
}

//Hila: for now - keeping data packets as is - so keeping this method as is
bool
ICTSync::Impl::update
  (const google::protobuf::RepeatedPtrField<Sync::SyncState >& content)
{
  NDN_LOG_DEBUG("ICTSync::Impl::update");
  int numUpdated = 0;
  for (size_t i = 0; i < content.size(); ++i)
  {
    if (content.Get(i).type() == Sync::SyncState_ActionType_UPDATE)
    {
      NDN_LOG_DEBUG("Data type: UPDATE");
      if (digestTree_->update
          (content.Get(i).name(), content.Get(i).seqno().session(),
           content.Get(i).seqno().seq()))
      {
        ++numUpdated;
        // if local digest updated
        if (applicationDataPrefixUri_ == content.Get(i).name())
          sequenceNo_ = content.Get(i).seqno().seq();
      }
    }
    else if (content.Get(i).type() == Sync::SyncState_ActionType_UPDATE_NO_NAME)
    {
      NDN_LOG_DEBUG("Data type: UPDATE_NO_NAME");
      std::string dataName = digestTree_->getSessionName( content.Get(i).seqno().session());
      NDN_LOG_DEBUG("UPDATE_NO_NAME for session: "
                 << content.Get(i).seqno().session()
                 << " with name: " << dataName);
      if(!dataName.empty())
      {
        if (digestTree_->update
            (dataName, content.Get(i).seqno().session(),
             content.Get(i).seqno().seq()))
        {
          ++numUpdated;
          // if local digest updated
          if (applicationDataPrefixUri_ == dataName)
            sequenceNo_ = content.Get(i).seqno().seq();
        }
      }

    }
  }
  if(numUpdated)
    return true;
  else
    return false;
}

void
ICTSync::Impl::getProducerPrefixes
  (vector<PrefixAndSessionNo>& prefixes) const
{
  prefixes.clear();
  prefixes.reserve(digestTree_->size());

  for (size_t i = 0; i < digestTree_->size(); ++i) {
    const ICTVectorState::Node& node = digestTree_->get(i);
    prefixes.push_back(PrefixAndSessionNo(node.getDataPrefix(), node.getSessionNo()));
  }
}

int
ICTSync::Impl::getProducerSequenceNo(const std::string& dataPrefix, int sessionNo) const
{
  int index = digestTree_->find(dataPrefix, sessionNo);
  if (index < 0)
    return -1;
  else
    return digestTree_->get(index).getSequenceNo();
}

// API - publish the new sequenceNo
void
ICTSync::Impl::publishNextSequenceNo(const Block& applicationInfo)
{
  NDN_LOG_DEBUG("publishNextSequenceNo");
  // update sequence numbers
  ++sequenceNo_;

  // update local vector state
  digestTree_->update(applicationDataPrefixUri_, sessionNo_,sequenceNo_);

  // broadcast sync Data to all pending interests
  broadcastSyncData();

  // send new long-lived interest
  Name intName(applicationBroadcastPrefix_);
  intName.append(digestTree_->getVectorRoot());

  sendSyncInterest(syncLifetime_);
  //sendSyncInterest(intName, syncLifetime_);

}

// On Interest callback
// void
// ICTSync::Impl::onInterest
//   (const std::shared_ptr<const Name>& prefix,
//    const std::shared_ptr<const Interest>& interest, Face& face,
//    uint64_t registerPrefixId,
//    const std::shared_ptr<const InterestFilter>& filter)
void
ICTSync::Impl::onInterest
(const InterestFilter& filter,
 const Interest& interest)
{
  if (!enabled_)
    // Ignore callbacks after the application calls shutdown().
    return;

  // Search if the digest already exists in the digest log.
  NDN_LOG_DEBUG("Sync Interest received in callback.");
  NDN_LOG_DEBUG(interest.getName().toUri());
  std::string iname = interest.getName().toUri();

  if (interest.getName().size() == applicationBroadcastPrefix_.size() + 2)
  {
    if(isDiscovery_)
      processDiscoveryInterest(interest, face_);
    else
      NDN_LOG_ERROR("Recieved DISCOVERY packet when discovery mode is off. Drop packet");
    return;
  }
  string syncDigest = interest.getName().get
    (applicationBroadcastPrefix_.size()).toUri();//EscapedString();

  if (syncDigest == "00")
  {
    // a newcomer interest.
    processNewcomerInterest(interest, syncDigest, face_);
  }
  else
  {
    // next line moved into processSyncInterest. Interest should be saved only
    // if it can't be satisfied now.
    //pendingInterests_.storeInterest(interest, face);

    if (unescape(syncDigest) != digestTree_->getVectorRoot())
    {
      processSyncInterest(interest, syncDigest, face_);
    }
  }
}

// void
// ICTSync::Impl::onData
//   (const std::shared_ptr<const Interest>& interest,
//    const std::shared_ptr<Data>& data)
void
ICTSync::Impl::onData
  (const Interest& interest,
   const Data& data)
{
  if (!enabled_)
    // Ignore callbacks after the application calls shutdown().
    return;

  NDN_LOG_DEBUG("Sync ContentObject received in callback");
  NDN_LOG_DEBUG("Data name: " + data.getName().toUri());
  string discoveryNameComponent = data.getName().get
    (applicationBroadcastPrefix_.size()).toUri();//EscapedString();
  //JP ADDED //this is because we only updating from the interest and ignoring any other sync packets when in discovery
  if(isDiscovery_ && discoveryNameComponent != "DISCOVERY" ) //&& digestTree_->getVectorRoot() != "00")
    {
      string iname = interest.getName().toUri();
      string dname = data.getName().toUri();
      //JP PROBLEM // also might need to make call back for discovery separate from regular sync data
      NDN_LOG_DEBUG("In Discovery but not discovery data - skipping data");
      //JP ADDED 11/10/19 - shouldn't this send out a new interest if the sync interest is eaten
      // express an up-to-date interest
      Name name(applicationBroadcastPrefix_);
      name.append(digestTree_->getVectorRoot());
      sendSyncInterest(syncLifetime_);
      //sendSyncInterest(name, syncLifetime_);
      return;
    }
  //END JP ADDED
  Sync::SyncStateMsg tempContent;
  tempContent.ParseFromArray(data.getContent().value(), data.getContent().value_size());
  //tempContent.ParseFromArray(data.getContent().getBuffer()->get<void>(), data.getContent().getBuffer()->size());
  NDN_LOG_DEBUG("Parsed content");
  const google::protobuf::RepeatedPtrField<Sync::SyncState >&content = tempContent.ss();
  NDN_LOG_DEBUG("Got content pointer");

  bool isUpdated;
  if(discoveryNameComponent == "DISCOVERY")
  {
    if(!isDiscovery_)
    {
      NDN_LOG_ERROR("received discovery data but discovery mode is off. Drop packet");
      return;
    }
    isUpdated = onDiscoveryData(interest, content);

  }
  else if (digestTree_->getVectorRoot() == "00") 
  {
    //processing initial sync d
    initialOnData(content);
    isUpdated = true;
  }
  else
  {
    isUpdated = update(content);
  }
  // update application only if there are updates
  if (isUpdated)
  {
    // prep updates to be sent to the app
    vector<SyncState> appUpdates;
    for (size_t i = 0; i < content.size(); ++i)
    {
      // Only report UPDATE sync states.
      if (content.Get(i).type() == Sync::SyncState_ActionType_UPDATE ||
          content.Get(i).type() == Sync::SyncState_ActionType_UPDATE_NO_NAME )
      {
        Block applicationInfo;
        if (content.Get(i).has_application_info() &&
            content.Get(i).application_info().size() > 0)
          applicationInfo = Block
            ((const uint8_t*)&content.Get(i).application_info()[0],
             content.Get(i).application_info().size());

        // get the sequence number from digest tree in case the sequnce
        // before discovery was greater than the one in the discovery packet
        std::string dataName;
        if (content.Get(i).type() == Sync::SyncState_ActionType_UPDATE_NO_NAME)
        {
          dataName = digestTree_->getSessionName(content.Get(i).seqno().session());
          if (dataName.empty())
          {
            NDN_LOG_ERROR("Couldn't get data Name for session " << content.Get(i).seqno().session() << " Can't update App");
            continue;
          }
        }
        else
          dataName = content.Get(i).name();

        int latestSeq = getProducerSequenceNo(dataName,content.Get(i).seqno().session());
        appUpdates.push_back(SyncState
          (dataName, content.Get(i).seqno().session(),
           latestSeq, applicationInfo));
      }
    }
    try {
      onReceivedSyncState_(appUpdates, false); // Hila: changed isRecovery to false
    } catch (const std::exception& ex) {
      NDN_LOG_ERROR("ICTSync::Impl::onData: Error in onReceivedSyncState: " << ex.what());
    } catch (...) {
      NDN_LOG_ERROR("ICTSync::Impl::onData: Error in onReceivedSyncState.");
    }
  }
  // express an up-to-date interest
  Name name(applicationBroadcastPrefix_);
  name.append(digestTree_->getVectorRoot());

  sendSyncInterest(syncLifetime_);
  //sendSyncInterest(name, syncLifetime_);

}

void
ICTSync::Impl::processNewcomerInterest
  (const Interest& interest, const string& syncDigest, Face& face)
{
  NDN_LOG_DEBUG("processNewcomerInterest");

  //JP Added
  if (noData_) return;
  //End JP Added
  //Hila: if local state is empty, quit
  if(digestTree_->getVectorRoot() == "00")
  {
    NDN_LOG_DEBUG("local is at initial state, nothing to respond");
    return;
  }

  // go over local syncTree and get the latest seqs
  Sync::SyncStateMsg tempContent;
  for (size_t i = 0; i < digestTree_->size(); ++i)
  {
    Sync::SyncState* content = tempContent.add_ss();
    content->set_name(digestTree_->get(i).getDataPrefix());
    content->set_type(Sync::SyncState_ActionType_UPDATE);
    content->mutable_seqno()->set_seq(digestTree_->get(i).getSequenceNo());
    content->mutable_seqno()->set_session(digestTree_->get(i).getSessionNo());
  }

  if (tempContent.ss_size() != 0)
  {
    std::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(tempContent.ByteSize()));
    tempContent.SerializeToArray(&array->front(), array->size());
    Data data(interest.getName());
    data.setContent(Block((const uint8_t*)array->data(),array->size()));//, false));

    // Limit the lifetime of replies to interest for "00" since they can be different.
    data.setFreshnessPeriod(time::milliseconds(500));

    //ndn::security::SigningInfo si;
    //si.setSigningIdentity(certificateName_);
    if (certificateName_.empty())
      keyChain_.sign(data);
    else   
      keyChain_.sign(data, security::signingByCertificate(certificateName_));//si);//certificateName_);
    //keyChain_.sign(data, ndn::security::signingByIdentity(certificateName_));//si);
    try {
      face.put(data);
      NDN_LOG_DEBUG("send newcomer data back");
      NDN_LOG_DEBUG(interest.getName().toUri());
    }
    catch (std::exception& e) {
      NDN_LOG_DEBUG(e.what());
    }
  }
}

// Process incoming sync interest
void
ICTSync::Impl::processSyncInterest
  (const Interest& interest, const string& syncDigest, Face& face)
{
  NDN_LOG_DEBUG("processSyncInterest: " + syncDigest);

  // Hila: Get index list of set-difference
  std::vector<uint8_t> localIndexListToSend;
  std::vector<std::tuple<uint32_t, uint32_t>> RemoteUpdates;
  std::vector<std::tuple<uint32_t, uint32_t>> unknownSessions;
  bool pushDataName;

  // GetDiff==-1 if localIndexListToSend is empty. should still check Remote updates.
  if(digestTree_->getDiff(syncDigest,
                          localIndexListToSend,
                          RemoteUpdates,
                          unknownSessions,
                          pushDataName) == -1)
  {
    // local doesn't have new updates and has nothing to send
    // save interest for future updates
    //JP Added
    if (!noData_)
    //End JP Added
      pendingInterests_.storeInterest(interest);//, face);
    NDN_LOG_DEBUG("Nothing to send. Saving interest for future updates");
  }
  else
  {
    // local has up-to-date  info. Satisfy the Interest with the set-difference
    NDN_LOG_DEBUG("Positive set-diff size is  " << localIndexListToSend.size()
               << " for incoming state " << syncDigest
               << ". About to send data to update remote. ");

    // send data according to the up-to-date items in local state
    sendSyncData(syncDigest, localIndexListToSend, face, pushDataName);
  }

  // update local state and application according to the up-to-date items in the remote state
  if(RemoteUpdates.size() > 0)
  {
    NDN_LOG_DEBUG("Negative set-diff size is  " << RemoteUpdates.size()
               << " for incoming state " << syncDigest
               << ". About to update local. ");
    processInterestUpdates(RemoteUpdates);
  }
  else
    NDN_LOG_DEBUG("no negative updates");

  if(unknownSessions.size() > 0)
  {
    if(isDiscovery_)
    {
      NDN_LOG_DEBUG("unknownSessions list size is  " << unknownSessions.size()
                 << " for incoming state " << syncDigest
                 << ". About to send discovery interests. ");
      processUnknownSessionIds(unknownSessions);
    }
  }
  else
    NDN_LOG_DEBUG("no unknown session ids");
}

void ICTSync::Impl::processInterestUpdates(std::vector<std::tuple<uint32_t, uint32_t>>& RemoteUpdates)
{
  NDN_LOG_DEBUG("processInterestUpdates");

  vector<SyncState> appUpdates;

  // go over all remote updates
  for (size_t i = 0; i < RemoteUpdates.size(); ++i)
  {
    // first, look if session id is recognized
    int sessionIndex = digestTree_->find(std::get<0>(RemoteUpdates[i]));
    if(sessionIndex >= 0)
    {
      NDN_LOG_DEBUG("processInterestUpdates: update session " <<  std::get<0>(RemoteUpdates[i])
                << " with seq " << std::get<1>(RemoteUpdates[i]));
      // update local state
      digestTree_->update(digestTree_->get(sessionIndex).getDataPrefix(),
                          std::get<0>(RemoteUpdates[i]),
                          std::get<1>(RemoteUpdates[i]));

      // an empty blob
      Block applicationInfo;

      // add to list to be sent to app
      appUpdates.push_back(SyncState
        (digestTree_->get(sessionIndex).getDataPrefix(),
         std::get<0>(RemoteUpdates[i]),
         std::get<1>(RemoteUpdates[i]),
         applicationInfo));
    }

  }
  // update the application
  try {
    onReceivedSyncState_(appUpdates, false); // Hila: changed isRecovery to false
  } catch (const std::exception& ex) {
    NDN_LOG_ERROR("ICTSync::Impl::processInterestUpdates: Error in onReceivedSyncState: " << ex.what());
  } catch (...) {
    NDN_LOG_ERROR("ICTSync::Impl::processInterestUpdates: Error in onReceivedSyncState.");
  }
  
  //JP ADDED 11/10/19 - shouldn't this send out a new interest if the digest changes?
  // send new long-lived interest
  Name intName(applicationBroadcastPrefix_);
  intName.append(digestTree_->getVectorRoot());

  sendSyncInterest(syncLifetime_);
  //sendSyncInterest(intName, syncLifetime_);
}

void ICTSync::Impl::processUnknownSessionIds(std::vector<std::tuple<uint32_t, uint32_t>>& unknownSessionIds)
{
  NDN_LOG_DEBUG("processUnknownSessionIds");

  if(!isDiscovery_)
  {
    NDN_LOG_ERROR("processUnknownSessionIds but discovery mode is off. Quit processing");
    return;
  }

  // send an interest for each unknown session id
  for (const auto &unknownSession : unknownSessionIds)
  {
    // first, check if an interest for this session is already in flight
    auto search = outgoingDiscoveryInterests_.find(std::get<0>(unknownSession));
    if (search != outgoingDiscoveryInterests_.end())
    {
      NDN_LOG_DEBUG("Discovery Interest for session " << std::get<0>(unknownSession) <<
                 " was sent before and is still pending. Wait for timeout to express again");

      // store the greatest sequnce number
      if (search->second < std::get<1>(unknownSession))
        outgoingDiscoveryInterests_[std::get<0>(unknownSession)] = std::get<1>(unknownSession);

      continue; //return; //JP change
    }

    // no discovery interest for this session is in flight
    Name iname = applicationBroadcastPrefix_;
    iname.append("DISCOVERY").append(std::to_string(std::get<0>(unknownSession)).c_str());
    Interest interest(iname);
    //interest.getName().append("DISCOVERY");
    //JP changed to make human readable in wireshark
    //interest.getName().append(std::to_string(std::get<0>(unknownSession)).c_str());//Name::Component::fromNumber(std::get<0>(unknownSession)));
    interest.setInterestLifetime(syncLifetime_);
    face_.expressInterest
      (interest, bind(&ICTSync::Impl::onData, shared_from_this(), _1, _2),
       bind(&ICTSync::Impl::discoveryNack, shared_from_this(), _1, _2),
       bind(&ICTSync::Impl::discoveryTimeout, shared_from_this(), _1));

    outgoingDiscoveryInterests_[std::get<0>(unknownSession)] = std::get<1>(unknownSession);

    NDN_LOG_DEBUG("Discovery Interest for session " << std::get<0>(unknownSession) << " expressed");
    NDN_LOG_DEBUG(interest.getName().toUri());
  }

}

void
ICTSync::Impl::processDiscoveryInterest
  (const Interest& interest, Face& face)
{
  NDN_LOG_DEBUG("processDiscoveryInterest");

  if(!isDiscovery_)
  {
    NDN_LOG_ERROR("received discovery interest but discovery mode is off. Drop packet");
    return;
  }
  // must be discovery interest
  string discoveryStr = interest.getName().get
    (applicationBroadcastPrefix_.size()).toUri();//EscapedString();
  if(discoveryStr != "DISCOVERY")
  {
    NDN_LOG_ERROR("Unknown interest format");
    return;
  }

  // Get the requetsed session id
  //JP change to get sessionID readable in wireshark
  uint32_t sessionId = (uint32_t)std::stoi(interest.getName().
					   get(applicationBroadcastPrefix_.size() + 1).toUri());//EscapedString()); //Number();
  //uint32_t sessionId = ::atoi(sessionIdStr.c_str());

  NDN_LOG_DEBUG("received DISCOVERY for session " << sessionId );

  // Check if session id is known
  int sessionIndex = digestTree_->find(sessionId);
  if(sessionIndex < 0)
  {
    NDN_LOG_ERROR("Unknown session " << sessionId << " in DISCOVERY interest. Interest dropped (not necessarily an error)");
    return;
  }
  int seq = digestTree_->get(sessionIndex).getSequenceNo();
  string dataName = digestTree_->get(sessionIndex).getDataPrefix();

  // respond to interest with data name and latest known sequence number
  NDN_LOG_TRACE("Session ID found, about to send DISCOVERY data " <<
             "sessionId: " << sessionId  <<
             ", data name: " << dataName <<
             ", seq number " << seq);

  // create data packet
  Sync::SyncStateMsg tempContent;
  Sync::SyncState* content = tempContent.add_ss();
  content->set_name(dataName);
  content->set_type(Sync::SyncState_ActionType_UPDATE);
  content->mutable_seqno()->set_seq(seq);
  content->mutable_seqno()->set_session(sessionId);

  std::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(tempContent.ByteSize()));
  tempContent.SerializeToArray(&array->front(), array->size());
  Data data(interest.getName());
  data.setContent(Block((const uint8_t*)array->data(), array->size()));//, false));
  //ndn::security::SigningInfo si;
  //si.setSigningIdentity(certificateName_);
    if (certificateName_.empty())
      keyChain_.sign(data);
    else   
      keyChain_.sign(data, security::signingByCertificate(certificateName_));//si);//certificateName_);
    //keyChain_.sign(data, security::signingByIdentity(certificateName_));//si);//certificateName_);
  try {
    face.put(data);
    NDN_LOG_DEBUG("Sync Data sent");
    NDN_LOG_DEBUG(data.getName().toUri());
  } catch (std::exception& e) {
    NDN_LOG_DEBUG(e.what());
  }
}

void
ICTSync::Impl::discoveryTimeout(const Interest& interest)
{
  NDN_LOG_DEBUG("discoveryTimeout for name " << interest.getName().toUri());
  if(!isDiscovery_)
  {
    NDN_LOG_ERROR("received discovery timeout but discovery mode is off. quit callback");
    return;
  }
  // must be discovery interest
  string discoveryStr = interest.getName().get
    (applicationBroadcastPrefix_.size()).toUri();//EscapedString();
  if(discoveryStr != "DISCOVERY")
  {
    NDN_LOG_ERROR("Unknown interest format");
    return;
  }
  // Get the requetsed session id
  uint32_t sessionId = (uint32_t)std::stoi(interest.getName().
					   get(applicationBroadcastPrefix_.size() + 1).toUri());//EscapedString());

  NDN_LOG_DEBUG("DISCOVERY Timeout for session " << sessionId );

  Interest interest2(interest.getName());
  interest2.setInterestLifetime(syncLifetime_);

  //JP change to resend but need to figure out what to do if never get an answer
  face_.expressInterest
    (interest2, bind(&ICTSync::Impl::onData, shared_from_this(), _1, _2),
     bind(&ICTSync::Impl::discoveryNack, shared_from_this(), _1, _2),
     bind(&ICTSync::Impl::discoveryTimeout, shared_from_this(), _1));
  // remove from outgoingDiscoveryInterests_
  //auto search = outgoingDiscoveryInterests_.find(sessionId);
  //if(search != outgoingDiscoveryInterests_.end())
  //    outgoingDiscoveryInterests_.erase(search);
  //else
  //  NDN_LOG_ERROR("DISCOVERY session timed out but it's not in outgoing discovery interest list" );

}

bool
ICTSync::Impl::sendSyncData
  (const string& syncDigest, std::vector<uint8_t>& indexListToSend, Face& face, bool sendName)
{
  //JP Added
  if (noData_)
    {
      NDN_LOG_DEBUG("sendSyncData noData_ set not sending name: " << syncDigest);
      return true;
    }
  //End JP Added
  NDN_LOG_DEBUG("sendSyncData with name: " << syncDigest);

  // create data packet
  Sync::SyncStateMsg tempContent;
  for (size_t i = 0; i < indexListToSend.size(); ++i)
  {
    Sync::SyncState* content = tempContent.add_ss();
    // If in discovery mode, then no need to send the name unless specifically requested.
    if (!isDiscovery_ || sendName)
    {
      content->set_name(digestTree_->get(indexListToSend[i]).getDataPrefix());
      content->set_type(Sync::SyncState_ActionType_UPDATE);
    }
    else
    {
      content->set_type(Sync::SyncState_ActionType_UPDATE_NO_NAME);
    }

    content->mutable_seqno()->set_seq(digestTree_->get(indexListToSend[i]).getSequenceNo());
    content->mutable_seqno()->set_session(digestTree_->get(indexListToSend[i]).getSessionNo());

    NDN_LOG_DEBUG("Sending diff. Session: " << digestTree_->get(indexListToSend[i]).getSessionNo()
               << " Sequence: " << digestTree_->get(indexListToSend[i]).getSequenceNo());
  }

  bool sent = false;
  if (tempContent.ss_size() != 0)
  {
    Name name(applicationBroadcastPrefix_);
    name.append(unescape(syncDigest));
    std::shared_ptr<vector<uint8_t> > array(new vector<uint8_t>(tempContent.ByteSize()));
    tempContent.SerializeToArray(&array->front(), array->size());
    Data data(name);
  //JP ADDED
    if (!isDiscovery_)
  //END JP ADDED
      data.setContent(Block((const uint8_t*)array->data(), array->size()));//, false));
    //ndn::security::SigningInfo si;
    //si.setSigningIdentity(certificateName_);
    if (certificateName_.empty())
      keyChain_.sign(data);//si);//certificateName_);
    else
      keyChain_.sign(data, security::signingByCertificate(certificateName_));//si);//certificateName_);
    //keyChain_.sign(data, security::signingByIdentity(certificateName_));//si);//certificateName_);
    try {
      face.put(data);
      sent = true;
      NDN_LOG_DEBUG("Sync Data sent");
      NDN_LOG_DEBUG(name.toUri());
    } catch (std::exception& e) {
      NDN_LOG_DEBUG(e.what());
    }
  }

  return sent;
}

// sync interest timeout callback
void
ICTSync::Impl::syncTimeout(const Interest& interest)
{
  if (!enabled_)
    // Ignore callbacks after the application calls shutdown().
    return;

  string iname = interest.getName().toUri();

   NDN_LOG_DEBUG("Sync Interest time out.");
   NDN_LOG_DEBUG("Timed out Interest name: " + interest.getName().toUri());
  string component = interest.getName().get
    (applicationBroadcastPrefix_.size()).toUri();//EscapedString();

  // if same state, retry the same interest.
  // Otherwise, assume someone else expressed the new interest
  // TBD_hila: should fix sendSyncInterest to clear existing pending interest
  // after fix, should think if this condition is still correct

  //JP PROBLEM
  NDN_LOG_DEBUG("Timeout Interest: " << unescape(component)
            << " local state: "  << digestTree_->getVectorRoot());
  if (unescape(component) == digestTree_->getVectorRoot())
  {
    Name name(interest.getName());
    //Name name(applicationBroadcastPrefix_);
    //name.append(digestTree_->getVectorRoot());
    //sendSyncInterest(syncLifetime_);
    sendSyncInterest(name, syncLifetime_);

  }
  else
    {
      string iname = interest.getName().toUri();
      NDN_LOG_DEBUG("Timeout Interest: don't recognize" << unescape(component)
		    << " local state: "  << digestTree_->getVectorRoot());
      //should check if this is my interest although it's unclear why we'd time out on someone else's
      //Name name(applicationBroadcastPrefix_);
      //name.append(digestTree_->getVectorRoot());
      //sendSyncInterest(name, syncLifetime_);
    }
}
// received discovery sync data with
bool
ICTSync::Impl::onDiscoveryData
    (const Interest& interest,
     const google::protobuf::RepeatedPtrField<Sync::SyncState >& content)
{
  if(!isDiscovery_)
  {
    NDN_LOG_ERROR("received discovery data but discovery mode is off. Quit");
    return false;
  }
  bool isUpdated = false;
  // Get the requetsed session id
  uint32_t sessionId = std::stoi(interest.getName().
    get(applicationBroadcastPrefix_.size() + 1).toUri()); //Number();

  NDN_LOG_TRACE("received DISCOVERY for session " << sessionId );

  // check if the sequence number recieved as unknown session and triggered
  // discovery is greater than the one received in the Discovery data
  int updateSeq = content.Get(0).seqno().seq();
  int savedSeq = (outgoingDiscoveryInterests_.find(content.Get(0).seqno().session()))->second;

  if(savedSeq > updateSeq)
    isUpdated = digestTree_->update(content.Get(0).name(), content.Get(0).seqno().session(),savedSeq);
  else
    isUpdated = digestTree_->update(content.Get(0).name(), content.Get(0).seqno().session(),updateSeq);

  return isUpdated;
}

// received intial sync data with "00"
void
ICTSync::Impl::initialOnData
  (const google::protobuf::RepeatedPtrField<Sync::SyncState >& content)
{
  NDN_LOG_DEBUG("initialOnData");
  update(content);

  try {
    onInitialized_();

  } catch (const std::exception& ex) {
    NDN_LOG_ERROR("ICTSync::Impl::initialOnData: Error in onInitialized: " << ex.what());
  } catch (...) {
    NDN_LOG_ERROR("ICTSync::Impl::initialOnData: Error in onInitialized.");
  }

  if (digestTree_->find(applicationDataPrefixUri_, sessionNo_) == -1)
  {
    // the user hasn't put himself in the digest tree.
    NDN_LOG_DEBUG("Add myself to digest");
    ++sequenceNo_;
    Sync::SyncStateMsg tempContent;
    Sync::SyncState* content2 = tempContent.add_ss();
    content2->set_name(applicationDataPrefixUri_);
    content2->set_type(Sync::SyncState_ActionType_UPDATE);
    content2->mutable_seqno()->set_seq(sequenceNo_);
    content2->mutable_seqno()->set_session(sessionNo_);

    if (update(tempContent.ss()))
    {
      try {
        onInitialized_();
      } catch (const std::exception& ex) {
        NDN_LOG_ERROR("ICTSync::Impl::initialOnData: Error in onInitialized: " << ex.what());
      } catch (...) {
        NDN_LOG_ERROR("ICTSync::Impl::initialOnData: Error in onInitialized.");
      }
    }
  }

}

// timeout callback for initialize interest with "00"
void
ICTSync::Impl::initialTimeout(const Interest& interest)
{
  if (!enabled_)
    // Ignore callbacks after the application calls shutdown().
    return;

  NDN_LOG_DEBUG("initial sync timeout");
  NDN_LOG_DEBUG("no other people");
  ++sequenceNo_;
  if (sequenceNo_ != initialPreviousSequenceNo_ + 1) {
    // Since there were no other users, we expect the sequence number to follow
    // the initial value.
    NDN_LOG_ERROR
      ("ChronoSync: sequenceNo_ is not the expected value for first use.");
    return;
  }

  Sync::SyncStateMsg tempContent;
  Sync::SyncState* content = tempContent.add_ss();
  content->set_name(applicationDataPrefixUri_);
  content->set_type(Sync::SyncState_ActionType_UPDATE);
  content->mutable_seqno()->set_seq(sequenceNo_);
  content->mutable_seqno()->set_session(sessionNo_);
  update(tempContent.ss());

  try {
    onInitialized_();
  } catch (const std::exception& ex) {
    NDN_LOG_ERROR("ICTSync::Impl::initialTimeout: Error in onInitialized: " << ex.what());
  } catch (...) {
    NDN_LOG_ERROR("ICTSync::Impl::initialTimeout: Error in onInitialized.");
  }

  Name name(applicationBroadcastPrefix_);
  name.append(digestTree_->getVectorRoot());

  //sendSyncInterest(syncLifetime_);
  sendSyncInterest(name, syncLifetime_);

}
// Send current sync Data to all pending interests
// this method is called when publishing new sequence.
// Compute the diff between pending Interests to current state
// and send local updates
// Do not do anything with negative list (remote updates) or unknownSessions
// because we already processed remote updates when receiving and
// storing the interest
void
ICTSync::Impl::broadcastSyncData()
{
  
  //JP ADDED
  if (noData_)
    {
      NDN_LOG_DEBUG("broadcastSyncData noData_ set not sending data");
      return;
    }
  //END JP ADDED
  NDN_LOG_DEBUG("broadcastSyncData");
  // get the list of pending interests
  std::vector<std::shared_ptr
             <const InterestList::PendingInterest> > pendingInterests;

  pendingInterests_.getInterestsWithPrefix(applicationBroadcastPrefix_,
                                              pendingInterests);

  NDN_LOG_DEBUG(pendingInterests.size());
  // Go over all pending interests, find out diff and send it.
  for (int i = (int)pendingInterests.size() - 1; i >= 0; --i)
  {
    NDN_LOG_DEBUG("Checking pending Interest: " << pendingInterests[i]->getInterest().getName() );

    // get diff
    string pendingDigest = pendingInterests[i]->getInterest().getName().get
      (applicationBroadcastPrefix_.size()).toUri();//EscapedString();

    pendingDigest = unescape(pendingDigest);

    // Get index list of set-difference
    std::vector<uint8_t> indexList;
    std::vector<std::tuple<uint32_t, uint32_t>> RemoteUpdates;
    std::vector<std::tuple<uint32_t, uint32_t>> unknownSessions;
    bool pushDataName;
    if(digestTree_->getDiff(pendingDigest, indexList,RemoteUpdates,unknownSessions,pushDataName) == -1)
    {
      NDN_LOG_DEBUG("No diff. quit");
    }
    else
    {
      NDN_LOG_DEBUG("set-diff size is  " << indexList.size()
                 << " for pending digest " << pendingDigest);
      if(!sendSyncData(pendingDigest, indexList, face_, pushDataName))//pendingInterests[i]->getFace(),pushDataName))
        NDN_LOG_ERROR("Failed to send Sync Data for pending: " << pendingDigest);

    }
  }
}

void ICTSync::Impl::sendSyncInterest(time::milliseconds syncLifetime)
{
  Name name(applicationBroadcastPrefix_);
  std::string sdigest = digestTree_->getVectorRoot();
  name.append(sdigest);
  if (syncUpdateInterval_.count() > 0)
    {
      std::chrono::milliseconds nowms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      if (nowms >= nextInterestTs_)
	{
	  lastSentDigest_ = sdigest;
	  sendSyncInterest(name, syncLifetime);
	  //nextInterestTs_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()) + syncUpdateInterval_;
	}
    }
  else
    {
      lastSentDigest_ = sdigest;
      sendSyncInterest(name, syncLifetime);
    }
}

void ICTSync::Impl::sendSyncInterest(Name& interestName,
				     time::milliseconds syncLifetime)
{
  Interest interest(interestName);
  interest.setInterestLifetime(syncLifetime);
  std::string iname = interestName.toUri();
  
  //if (lastInterestId_)
    lastInterestId_.cancel();
  
  //PendingInterestHandle newInterestID
  lastInterestId_  = face_.expressInterest(interest,
							       bind(&ICTSync::Impl::onData, shared_from_this(), _1, _2),
							       bind(&ICTSync::Impl::syncNack, shared_from_this(), _1, _2),
							       bind(&ICTSync::Impl::syncTimeout, shared_from_this(), _1));
  if (syncUpdateInterval_.count() > 0)
    {
      nextInterestTs_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()) + syncUpdateInterval_;
      scheduler_->schedule(time::milliseconds(syncUpdateInterval_.count()), bind(&ICTSync::Impl::checkForUpdate, this));
    }
  
  //lastInterestId_ = newInterestID;
  NDN_LOG_DEBUG("Syncinterest expressed:");
  NDN_LOG_DEBUG(interest.getName().toUri());
  
}

void ICTSync::Impl::checkForUpdate()
{
  if (digestTree_->getVectorRoot() != lastSentDigest_)
    {
      sendSyncInterest(syncLifetime_);
      NDN_LOG_DEBUG("checkForUpdate: state changed calling sendSyncInterest");
    }
  else
    {
      NDN_LOG_DEBUG("checkForUpdate: no state change");
    }
  scheduler_->schedule(time::milliseconds(syncUpdateInterval_.count()), bind(&ICTSync::Impl::checkForUpdate, this));
}

}

