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

#ifndef NDN_ICT_SYNC_HPP
#define NDN_ICT_SYNC_HPP

#include <vector>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include "pending-interests.hpp"
#include <chrono>

namespace google { namespace protobuf { template <typename Element> class RepeatedPtrField; } }
namespace Sync { class SyncStateMsg; }
namespace Sync { class SyncState; }

using namespace ndn;
namespace ict {

class ICTVectorState;

/**
 * ICTSync implements
 */
class ICTSync {
public:
  class SyncState;
  typedef std::function<void
    (const std::vector<ICTSync::SyncState>& syncStates, bool isRecovery)>
      OnReceivedSyncState;

  typedef std::function<void()> OnInitialized;

  /**
   * Create a new ICTSync to communicate using the given face.
   */
  ICTSync
    (const OnReceivedSyncState& onReceivedSyncState,
     const OnInitialized& onInitialized, const Name& applicationDataPrefix,
     const Name& applicationBroadcastPrefix, int sessionNo,
     Face& face, KeyChain& keyChain, const Name& certificateName,
     time::milliseconds syncLifetime, const  RegisterPrefixFailureCallback& onRegisterFailed,
     int previousSequenceNumber = -1, bool isDiscovery = false, bool noData = false, std::chrono::milliseconds syncUpdateInt=std::chrono::milliseconds(0))
  : impl_(new Impl
      (onReceivedSyncState, onInitialized, applicationDataPrefix,
       applicationBroadcastPrefix, sessionNo, face, keyChain, certificateName,
       syncLifetime, previousSequenceNumber, isDiscovery, noData, syncUpdateInt))
  {
    impl_->initialize(onRegisterFailed);
  }
  //JP added to be called if register fails
  void
  reRegister(const  RegisterPrefixFailureCallback& onRegisterFailed)
  {
    impl_->reRegister(onRegisterFailed);
  }

  /**
   * A SyncState holds the values of a sync state message which is passed to the
   * onReceivedSyncState callback which was given to the ChronoSyn2013
   * constructor. Note: this has the same info as the Protobuf class
   * Sync::SyncState, but we make a separate class so that we don't need the
   * Protobuf definition in the ChronoSync API.
   */
  class SyncState {
  public:
    SyncState
      (const std::string& dataPrefixUri, int sessionNo, int sequenceNo,
       const Block& applicationInfo)
    : dataPrefixUri_(dataPrefixUri), sessionNo_(sessionNo),
      sequenceNo_(sequenceNo), applicationInfo_(applicationInfo)
    {
    }

    /**
     * Get the application data prefix.
     * @return The application data prefix as a Name URI string.
     */
    const std::string&
    getDataPrefix() const { return dataPrefixUri_; }

    /**
     * Get the session number associated with the application data prefix.
     * @return The session number.
     */
    int
    getSessionNo() const { return sessionNo_; }

    /**
     * Get the sequence number for this sync state message.
     * @return The sequence number.
     */
    int
    getSequenceNo() const { return sequenceNo_; }

    /**
     * Get the application info which was included when the sender published
     * the next sequence number.
     * @return The applicationInfo Block. If the sender did not provide any,
     * return an isNull Blob.
     */
    const Block&
    getApplicationInfo() const { return applicationInfo_; }

  private:
    std::string dataPrefixUri_;
    int sessionNo_;
    int sequenceNo_;
    Block applicationInfo_;
  };

  /**
   * A PrefixAndSessionNo holds a user's data prefix and session number (used to
   * return a list from getProducerPrefixes).
   */
  class PrefixAndSessionNo {
  public:
    PrefixAndSessionNo(const std::string& dataPrefixUri, int sessionNo)
    : dataPrefixUri_(dataPrefixUri), sessionNo_(sessionNo)
    {
    }

    /**
     * Get the application data prefix for this sync state message.
     * @return The application data prefix as a Name URI string.
     */
    const std::string&
    getDataPrefix() const { return dataPrefixUri_; }

    /**
     * Get the session number associated with the application data prefix for
     * this sync state message.
     * @return The session number.
     */
    int
    getSessionNo() const { return sessionNo_; }

  private:
    std::string dataPrefixUri_;
    int sessionNo_;
  };

  /**
   * Get a copy of the current list of producer data prefixes, and the
   * associated session number. You can use these in getProducerSequenceNo().
   * This includes the prefix for this user.
   * @param prefixes This clears the vector and adds a copy of each producer
   * prefix and session number.
   */
  void
  getProducerPrefixes(std::vector<PrefixAndSessionNo>& prefixes) const
  {
    impl_->getProducerPrefixes(prefixes);
  }

  /**
   * Get the current sequence number in the digest tree for the given
   * producer dataPrefix and sessionNo.
   * @param dataPrefix The producer data prefix as a Name URI string.
   * @param sessionNo The producer session number.
   * @return The current producer sequence number, or -1 if the producer
   * namePrefix and sessionNo are not in the digest tree.
   */
  int
  getProducerSequenceNo(const std::string& dataPrefix, int sessionNo) const
  {
    return impl_->getProducerSequenceNo(dataPrefix, sessionNo);
  }

  /**
   * Increment the sequence number, create a sync message with the new
   * sequence number and publish a data packet where the name is
   * the applicationBroadcastPrefix + the root digest of the current digest
   * tree. Then add the sync message to the digest tree and digest log which
   * creates a new root digest. Finally, express an interest for the next sync
   * update with the name applicationBroadcastPrefix + the new root digest.
   * After this, your application should publish the content for the new
   * sequence number. You can get the new sequence number with getSequenceNo().
   * @note Your application must call processEvents. Since processEvents
   * modifies the internal ChronoSync data structures, your application should
   * make sure that it calls processEvents in the same thread as
   * publishNextSequenceNo() (which also modifies the data structures).
   * @param applicationInfo (optional) This appends applicationInfo to the
   * content of the sync messages. This same info is provided to the receiving
   * application in the SyncState state object provided to the
   * onReceivedSyncState callback.
   */
  void
  publishNextSequenceNo(const Block& applicationInfo = Block())
  {
    return impl_->publishNextSequenceNo(applicationInfo);
  }

  /**
   * Get the sequence number of the latest data published by this application
   * instance.
   * @return The sequence number.
   */
  int
  getSequenceNo() const
  {
    return impl_->getSequenceNo();
  }

  /**
   * Unregister callbacks so that this does not respond to interests anymore.
   * If you will delete this ICTSync object while your application is
   * still running, you should call shutdown() first.  After calling this, you
   * should not call publishNextSequenceNo() again since the behavior will be
   * undefined.
   * @note Because this modifies internal ChronoSync data structures, your
   * application should make sure that it calls processEvents in the same
   * thread as shutdown() (which also modifies the data structures).
   */
  void
  shutdown()
  {
    impl_->shutdown();
  }

private:

  /**
   * ICTSync::Impl does the work of ICTSync. It is a separate
   * class so that ICTSync can create an instance in a shared_ptr to
   * use in callbacks.
   */
  class Impl : public std::enable_shared_from_this<Impl> {
  public:
    /**
     * Create a new Impl, which should belong to a shared_ptr. Then you must
     * call initialize().  See the ICTSync constructor for parameter
     * documentation.
     */
    Impl
      (const OnReceivedSyncState& onReceivedSyncState,
       const OnInitialized& onInitialized, const Name& applicationDataPrefix,
       const Name& applicationBroadcastPrefix, int sessionNo,
       Face& face, KeyChain& keyChain, const Name& certificateName,
       time::milliseconds syncLifetime, int previousSequenceNumber, bool isDiscovery, bool noData, std::chrono::milliseconds syncUpdateInt);

    /**
     * Register the applicationBroadcastPrefix to receive interests for sync
     * state messages and express an interest for the initial.
     * You must call this after creating this Impl and making it belong to
     * a shared_ptr. This is a separate method from the constructor because
     * we need to call shared_from_this(), but in the constructor this object
     * does not yet belong to a shared_ptr.
     */
    void
    initialize(const  RegisterPrefixFailureCallback& onRegisterFailed);
    //JP added to be called if register fails
    void
    reRegister(const  RegisterPrefixFailureCallback& onRegisterFailed);

    /**
     * See ICTSync::getProducerPrefixes.
     */
    void
    getProducerPrefixes(std::vector<PrefixAndSessionNo>& prefixes) const;

    /**
     * See ICTSync::getProducerSequenceNo.
     */
    int
    getProducerSequenceNo(const std::string& dataPrefix, int sessionNo) const;

    /**
     * See ICTSync::publishNextSequenceNo.
     */
    void
    publishNextSequenceNo(const Block& applicationInfo);

    /**
     * See ICTSync::getSequenceNo.
     */
    int
    getSequenceNo() const { return sequenceNo_; }

    /**
     * See ICTSync::shutdown.
     */
    void
    shutdown();

  private:
    /**
    * Express an interest.
    @param interest name.
    @param interest lifet time
    */
    void sendSyncInterest(Name& interestName,
                          time::milliseconds syncLifetime);
    /**
    * Express an interest.
    @param interest lifet time
    */
    void sendSyncInterest(time::milliseconds syncLifetime);

   /**
    * Go over the list of pending interests, and calculate the set-difference
    * between local state and pending interest state. create a data packet for
    * each set-diff, sign and send. The name of all data packets
    * is applicationBroadcastPrefix_ + pedning vector state
    */
    void
    broadcastSyncData();


    /**
     * Update the digest tree with the messages in content.
     * @param content The sync state messages.
     * @return True if new sequence number was recorded for
     * for at least one participant (needed to update app
     * only with new updates)
     */
    bool
    update(const google::protobuf::RepeatedPtrField<Sync::SyncState >& content);

    /**
     * Process the sync interest from the applicationBroadcastPrefix. If we can't
     * satisfy the interest, add it to the pending interest table in the
     * pendingInterests_ so that a future call to add may satisfy it.
     */
    void
    onInterest(const InterestFilter& filter,
	       const Interest& interest);//, Face& face);

    // Process Sync Data.
    void
    onData
      (const Interest& interest,
       const Data& data);

    // process discovery data
    // void
    // onDiscoveryData
    //   (const std::shared_ptr<const Interest>& interest,
    //    const std::shared_ptr<Data>& data);

    // Initial sync interest timeout, which means there are no other publishers yet.
    void
    initialTimeout(const Interest& interest);

    //for now handle Nack like timeout - JP
    void
    initialNack(const Interest& interest, const lp::Nack& nack){ initialTimeout(interest);}

    void
    processNewcomerInterest
      (const Interest& interest, const std::string& syncDigest, Face& face);

    void
    processDiscoveryInterest
      (const Interest& interest, Face& face);

    void
    discoveryTimeout(const Interest& interest);

    //for now handle Nack like timeout - JP
    void
    discoveryNack(const Interest& interest, const lp::Nack& nack) { discoveryTimeout(interest);}



    /**
     * Common interest processing, using vector state find the difference.
     */
    void
    processSyncInterest(const Interest& interest,
                        const std::string& syncDigest,
                        Face& face);

    bool
    sendSyncData (const std::string& syncDigest, std::vector<uint8_t>& indexListToSend, Face& face, bool sendName);

    void
    processInterestUpdates(std::vector<std::tuple<uint32_t, uint32_t>>& RemoteUpdates);

    void
    processUnknownSessionIds(std::vector<std::tuple<uint32_t, uint32_t>>& unknownSessionIds);
    // Sync interest time out, if the interest is the static one send again.
    void
    syncTimeout(const Interest& interest);
    
    //for now handle Nack like timeout - JP
    void
    syncNack(const Interest& interest, const lp::Nack& nack) { syncTimeout(interest);}

    // Process initial data which usually includes all other publisher's info, and send back the new comer's own info.
    void
    initialOnData(const google::protobuf::RepeatedPtrField<Sync::SyncState >& content);

    bool
    onDiscoveryData
        (const Interest& interest,
         const google::protobuf::RepeatedPtrField<Sync::SyncState >& content);

    void
    initialOndataOLD(const google::protobuf::RepeatedPtrField<Sync::SyncState >& content);

    void //checks for state change at interval of syncInterval
    checkForUpdate();


    Face& face_;
    KeyChain& keyChain_;
    Name certificateName_;
    time::milliseconds syncLifetime_;
    std::chrono::milliseconds syncUpdateInterval_;
    std::chrono::milliseconds nextInterestTs_;
    OnReceivedSyncState onReceivedSyncState_;
    OnInitialized onInitialized_;
    std::shared_ptr<ICTVectorState> digestTree_;
    std::string applicationDataPrefixUri_;
    const Name applicationBroadcastPrefix_;
    int sessionNo_;
    int initialPreviousSequenceNo_;
    int sequenceNo_;
    InterestList pendingInterests_;
    bool enabled_;
    ScopedPendingInterestHandle lastInterestId_;
    RegisteredPrefixHandle broadcastPrefixRegId_;
    std::map<int, int> outgoingDiscoveryInterests_;
    bool isDiscovery_;
    bool noData_;
    std::string lastSentDigest_;
    unique_ptr<ndn::Scheduler> scheduler_;            // scheduler
  };

  std::shared_ptr<Impl> impl_;
};

}

#endif
