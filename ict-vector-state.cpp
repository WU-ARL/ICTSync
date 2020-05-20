/*
 * Copyright (c) 2018-2022 Hila Ben Abraham and Jyoti Parwatikar
 * and Washington University in St. Louis
*/
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/**
 * Derived from ChronoSync2013 Jeff Thompson <jefft0@remap.ucla.edu>
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

#include <algorithm>
#include <ndn-cxx/util/logger.hpp>
//#if NDN_CPP_HAVE_LIBCRYPTO
#include <openssl/ssl.h>
//#else
//#include "../../contrib/openssl/sha.h"
//#endif
#include "ict-vector-state.hpp"
#include <boost/fusion/adapted/std_tuple.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/include/qi.hpp>
#include <tuple>


NDN_LOG_INIT(ict.ICTVectorState);
using namespace std;
using namespace ndn;
namespace ict {

/**
 * Convert the hex string to bytes and call SHA256_Update.
 * @param context The SHA256_CTX to update.
 * @param hex The hex string.
 * @return The result from SHA256_Update.
 */
static int
SHA256_UpdateHex(SHA256_CTX *context, const string& hex)
{
  vector<uint8_t> data(hex.size() / 2);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = (uint8_t)(16 * fromHexChar(hex[2 * i]) + fromHexChar(hex[2 * i + 1]));

  return SHA256_Update(context, &data[0], data.size());
}

bool
ICTVectorState::update(const std::string& dataPrefix, int sessionNo, int sequenceNo)
{
  int index = find(dataPrefix, sessionNo);
  NDN_LOG_DEBUG(dataPrefix << ", " << sessionNo);
  NDN_LOG_DEBUG("ICTVectorState::update session " << sessionNo << ", index " << index);
  if (index >= 0) {
    // only update the newer status
    if (digestNode_[index]->getSequenceNo() < sequenceNo)
      digestNode_[index]->setSequenceNo(sequenceNo);
    else
      return false;
  }
  else {
    NDN_LOG_DEBUG("new comer " << dataPrefix << ", session " << sessionNo <<
               ", sequence " << sequenceNo);
    // Insert into digestnode_ sorted.
    std::shared_ptr<Node> temp(new Node(dataPrefix, sessionNo, sequenceNo));
    digestNode_.insert
      (std::lower_bound(digestNode_.begin(), digestNode_.end(), temp, nodeCompare_),
       temp);
  }

  recomputeVectorRoot();
  return true;
}
void ICTVectorState::recomputeVectorRoot()
{
  std::string tempRoot;
  for (size_t i = 0; i < digestNode_.size(); ++i)
  {
    tempRoot.append(digestNode_[i]->getUserDigest());
  }


  vectorRoot_ = tempRoot;
  //vectorRoot_ = toHex((uint8_t*)tempRoot.c_str(), sizeof((uint8_t*)tempRoot.c_str()));
  NDN_LOG_DEBUG("update root to: " + vectorRoot_);
}

int
ICTVectorState::find(int sessionNo) const
{
  for (size_t i = 0; i < digestNode_.size(); ++i) {
    if (digestNode_[i]->getSessionNo() == sessionNo)
      return i;
  }

  return -1;
}
int
ICTVectorState::find(const string& dataPrefix, int sessionNo) const
{
  for (size_t i = 0; i < digestNode_.size(); ++i) {
    if (digestNode_[i]->getDataPrefix() == dataPrefix &&
        digestNode_[i]->getSessionNo() == sessionNo)
      return i;
  }

  return -1;
}
const std::string
ICTVectorState::getSessionName(int sessionNo) const
{
  for (size_t i = 0; i < digestNode_.size(); ++i) {
    if (digestNode_[i]->getSessionNo() == sessionNo)
      return digestNode_[i]->getDataPrefix();
  }
  NDN_LOG_DEBUG("Could not find session " << sessionNo << " Return empty string");
  return {};
}

// This method finds the set-difference between the local state and rState
// input parameter: rState
// output parameter:
//      positiveLocalIndexes: list of local state indexes that are up-to-date
//                            in local but not in remote
//      negativeLocalIndexes: list of rState indexes and their sequences that
//                            are out-of-date in local,
//      unknownSessions: list of sequences in remote that do not exist
//                            in local,
//      pushLocalSessions:  boolean flag indicates if local sessions were not foundInLocal
//                          in remote digest. This should only return true if
//                          a local session id was not found in remote. Not if the
//                          local sequence number is up-to-date

int
ICTVectorState::getDiff(const std::string& rState,
                    std::vector<uint8_t>& positiveLocalIndexes,
                    std::vector<std::tuple<uint32_t, uint32_t>>& negativeInLocal,
                    std::vector<std::tuple<uint32_t, uint32_t>>& unknownSessions,
                    bool pushLocalSessions) const
                    //std::vector<uint32_t>& unknownSessions) const
{
  pushLocalSessions = false;
  NDN_LOG_DEBUG("In getDiff ");

  positiveLocalIndexes.clear();
  negativeInLocal.clear();
  unknownSessions.clear();

  // parse rState into
  std::string nodeDelimiter = ";";
  std::string dataDelimiter = ",";

  std::string tmpRState(unescape(rState));

  //NDN_LOG_DEBUG("tmpRState: " + tmpRState);

  using tpl = std::tuple<uint32_t, uint32_t>;

  boost::spirit::qi::rule<std::string::iterator, tpl()> parse_into_tuple =
    boost::spirit::qi::int_ >> ',' >> boost::spirit::qi::int_;

  boost::spirit::qi::rule<std::string::iterator, std::vector<tpl>() > parse_into_vec = parse_into_tuple % ';';

  std::vector<tpl> remoteVector;
  bool b = boost::spirit::qi::parse(tmpRState.begin(), tmpRState.end(), parse_into_vec, remoteVector);
  for (const auto &t : remoteVector)
  {
      NDN_LOG_DEBUG("Remote parsed: " << std::get<0>(t) << ", " << std::get<1>(t));
  }
  NDN_LOG_DEBUG("Local state is: " <<   vectorRoot_);
  // Go over local tree
  // if local has something that remote doesn't: add to index list
  // if local has an up-to-date seq of a recognized local - add to index list
  for (size_t i = 0; i < digestNode_.size(); ++i)
  {
    NDN_LOG_DEBUG("Local node session is: " <<   digestNode_[i]->getSessionNo());
    bool foundInLocal = false;

    // TBD: change the for to while r.session < digestNode_[i]
    // because both lists should be sorted
    for (const auto &r : remoteVector)
    {
      NDN_LOG_DEBUG("Remote node session is: " << std::get<0>(r));
      if (digestNode_[i]->getSessionNo() == std::get<0>(r))
      {
        NDN_LOG_DEBUG("found remote session in local ");
        // only add to positiveLocalIndexes if local is newer than recieved
        if (digestNode_[i]->getSequenceNo() > std::get<1>(r))
        {
          positiveLocalIndexes.push_back(i);
          NDN_LOG_DEBUG("local seq (" << digestNode_[i]->getSequenceNo() <<
                    ")is higher than remote(" << std::get<1>(r) << ")");
        }
        // if remote seq is greater up-to-date add to negativeInLocal
        if (digestNode_[i]->getSequenceNo() < std::get<1>(r))
        {
          negativeInLocal.push_back(std::make_tuple(std::get<0>(r), std::get<1>(r)));
          NDN_LOG_DEBUG("local seq (" << digestNode_[i]->getSequenceNo() <<
                    ")is lower than remote(" << std::get<1>(r) << ")");
        }

        foundInLocal = true;
        break;
      }

    }
    if (!foundInLocal)
    {
      pushLocalSessions = true;
      NDN_LOG_DEBUG("local was not found in remote. Adding to response");
      positiveLocalIndexes.push_back(i);
    }
  }

  // go over remoteVector to check if there is something in remote that is
  // unknown to local. If so, add to negative list
  for (const auto &r : remoteVector)
  {
    // if remote session id not found in local
    if(find(std::get<0>(r)) == -1)
      unknownSessions.push_back(std::make_tuple(std::get<0>(r), std::get<1>(r)));
      //unknownSessions.push_back(std::get<0>(r));
  }


  if(positiveLocalIndexes.size() > 0)
    return positiveLocalIndexes.size();
  else
    return -1;
}

void
ICTVectorState::Node::recomputeUserDigest()
{
  // For now, encode a digest to be simply
  // "DataName,sessionNo,SeqNum"

  std::string userDigest;
  userDigest.append(std::to_string(sessionNo_));
  userDigest.append(",").append(std::to_string(sequenceNo_));
  userDigest.append(";");
  userDigest_ = userDigest;

}
void
ICTVectorState::Node::int32ToLittleEndian(uint32_t value, uint8_t* result)
{
  for (size_t i = 0; i < 4; i++) {
    result[i] = value % 256;
    value = value / 256;
  }
}

}
