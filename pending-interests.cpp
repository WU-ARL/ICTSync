/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */

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

//#include <algorithm>
#include <chrono>
#include <ndn-cxx/util/logger.hpp>
#include "pending-interests.hpp"



NDN_LOG_INIT(ict.PendingInterests);
using namespace std;
using namespace ndn;

namespace ict {

InterestList::Impl::Impl()  
{
}


void
InterestList::Impl::storeInterest(const Interest& interest)
{
  interests_.push_back(std::shared_ptr<PendingInterest>
    (new PendingInterest(interest)));
}

void
InterestList::Impl::getInterestsForName(const Name& name,
                                        vector<std::shared_ptr<const PendingInterest> >& pendingInterests,
                                        bool remove)
{
  pendingInterests.clear();

  // Remove timed-out interests as we add results.
  // Go backwards through the list so we can erase entries.
  long now = std::chrono::duration_cast< std::chrono::milliseconds >(
                                                                     std::chrono::system_clock::now().time_since_epoch()
                                                                                          ).count();
  for (int i = (int)interests_.size() - 1; i >= 0; --i) {
    if (interests_[i]->isTimedOut(now)) {
      interests_.erase(interests_.begin() + i);
    }
    else if (interests_[i]->getInterest().getName().equals(name))
      {
        pendingInterests.push_back(interests_[i]);
        if (remove)
          interests_.erase(interests_.begin() + i);
      }
  }
}

void
InterestList::Impl::getInterestsWithPrefix(const Name& prefix,
                                           vector<std::shared_ptr<const PendingInterest> >& pendingInterests,
                                           bool remove)
{
  pendingInterests.clear();

  // Remove timed-out interests as we add results.
  // Go backwards through the list so we can erase entries.
  long now = std::chrono::duration_cast< std::chrono::milliseconds >(
                                                                     std::chrono::system_clock::now().time_since_epoch()
                                                                     ).count();
  for (int i = (int)interests_.size() - 1; i >= 0; --i) {
    if (interests_[i]->isTimedOut(now)) {
      interests_.erase(interests_.begin() + i);
    }
    else if (prefix.isPrefixOf(interests_[i]->getInterest().getName()))
      {
        pendingInterests.push_back(interests_[i]);
        if (remove)
          interests_.erase(interests_.begin() + i);
      }
  }
}


InterestList::PendingInterest::PendingInterest
  (const Interest& interest)
    : interest_(interest), name_(interest.getName())
{

  time_start_ = std::chrono::duration_cast< std::chrono::milliseconds >(
			std::chrono::system_clock::now().time_since_epoch()
                                                                        ).count();
  // Set up timeout time.
  if (interest_.getInterestLifetime().count() >= 0)
    timeout_ms_ = time_start_ +
      interest_.getInterestLifetime().count();
  else
    // No timeout.
    timeout_ms_ = 0;
}

}
