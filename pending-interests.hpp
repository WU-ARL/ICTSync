/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019-2023 Jyoti Parwatikar 
 * and Washington University in St. Louis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifndef ICT_PENDING_INTERESTS_HPP
#define ICT_PENDING_INTERESTS_HPP

#include <vector>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/interest.hpp>

using namespace ndn;
namespace ict {

  class InterestList {
  public:

  /**
   * A PendingInterest holds an interest which onInterest received but could
   * not satisfy. When we add a new data packet to the cache, we will also check
   * if it satisfies a pending interest.
   */
    class PendingInterest {
    public:
      /**
       * Create a new PendingInterest and set the timeoutTime_ based on the
       * current time and the interest lifetime.
       * @param interest A shared_ptr for the interest.
       * @param face The face from the onInterest callback. If the
       * interest is satisfied later by a new data packet, we will send the data
       * packet to the face.
       */
      PendingInterest
      (const Interest& interest);
      
      /**
       * Return the interest given to the constructor. You must not modify this
       * object - if you need to modify it then make a copy.
       */
      const Interest&
      getInterest() const { return interest_; }
      
      const Name&
      getName() const
      {
        return name_;
      }
    public:
      
      /**
       * Return the time when this pending interest entry was created (the time
       * when the unsatisfied interest arrived and was added to the pending
       * interest table). The interest timeout is based on this value.
       * @return The timeout period start time in milliseconds since 1/1/1970
       */
      long
      getTimeStart() const { return time_start_; }


      /**
       * Check if this interest is timed out.
       * @param nowMilliseconds The current time in milliseconds from
       * ndn_getNowMilliseconds.
       * @return true if this interest timed out, otherwise false.
       */
      bool
      isTimedOut(long current) const
      {
        return timeout_ms_ > 0 &&
          current >= timeout_ms_;
      }
      
    private:
      const Interest interest_;
      const Name name_;
      long time_start_;
      long timeout_ms_; /**< The time when the
                                              * interest times out in ms or 0 for no timeout. */
    };


    InterestList():impl_(new Impl()){}

    /**
   * Remove timed-out pending interests, then for each pending interest which
   * matches according to prefix.isPrefixOf(interest.getName()), append the
   * PendingInterest entry to the given pendingInterests list. Note that
   * interest selectors are ignored. (To get interests which would match a
   * given data packet name, see getPendingInterestsForName().)
   * Because this modifies the internal tables, you should call this on the same
   * thread as processEvents, which can also modify the tables.
   * @param prefix The prefix of the interest names to match.
   * @param pendingInterests The vector to receive the matching PendingInterest
   * objects. This first clears the list before adding objects. You should not
   * modify the PendingInterest objects.
   */
  void
  getInterestsWithPrefix
    (const Name& prefix,
     std::vector<std::shared_ptr<const PendingInterest> >& pendingInterests)
  {
    impl_->getInterestsWithPrefix(prefix, pendingInterests);
  }
    
  /**
   * Store an interest from an OnInterest callback in the internal pending
   * interest table (normally because there is no Data packet available yet to
   * satisfy the interest). add(data) will check if the added Data packet
   * satisfies any pending interest and send it through the face.
   * Because this modifies the internal tables, you should call this on the same
   * thread as processEvents, which can also modify the tables.
   * @param interest The Interest for which we don't have a Data packet yet. You
   * should not modify the interest after calling this.
   * @param face The Face with the connection which received the
   * interest. This comes from the onInterest callback.
   */
  void
  storeInterest
    (const Interest& interest)
  {
    impl_->storeInterest(interest);
  }
  /**
   * Remove timed-out pending interests, then for each pending interest which
   * matches according to Interest.matchesName(name), append the PendingInterest
   * entry to the given pendingInterests list. (To get interests with a given
   * prefix, see getPendingInterestsWithPrefix().)
   * Because this modifies the internal tables, you should call this on the same
   * thread as processEvents, which can also modify the tables.
   * @param name The name to check.
   * @param pendingInterests The vector to receive the matching PendingInterest
   * objects. This first clears the list before adding objects. You should not
   * modify the PendingInterest objects.
   */
  void
  getInterestsForName
    (const Name& name,
     std::vector<std::shared_ptr<const PendingInterest> >& pendingInterests)
  {
    impl_->getInterestsForName(name, pendingInterests);
  }
private:
  /**
   * MemoryContentCache::Impl does the work of MemoryContentCache. It is a
   * separate class so that MemoryContentCache can create an instance in a
   * shared_ptr to use in callbacks.
   */
    class Impl : public std::enable_shared_from_this<Impl> {
    public:
      /**
       * Create a new Impl, which should belong to a shared_ptr. 
       */
      Impl();
      
      // void
      // add(const Data& data);

      void
      storeInterest
      (const Interest& interest);//, Face& face);
      
      void
      getInterestsForName
      (const Name& name,
       std::vector<std::shared_ptr<const PendingInterest> >& pendingInterests, bool remove=false);

      void
      getInterestsWithPrefix
      (const Name& prefix,
       std::vector<std::shared_ptr<const PendingInterest> >& pendingInterests, bool remove=false);
    private:
      std::vector<std::shared_ptr<const PendingInterest> > interests_;
    };

  std::shared_ptr<Impl> impl_;
};

}

#endif //ICT_PENDING_INTERESTS_HPP
