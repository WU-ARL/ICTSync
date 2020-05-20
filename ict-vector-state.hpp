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

#ifndef NDN_ICT_VECTOR_STATE_HPP
#define NDN_ICT_VECTOR_STATE_HPP

//#include <ndn-cpp/common.hpp>
#include <sstream>
#include <boost/iostreams/filtering_streambuf.hpp>
//#include <boost/iostreams/copy.hpp>
//#include <boost/iostreams/filter/gzip.hpp>
#include <string>

namespace ict {
class ICTVectorState {
public:
  ICTVectorState()
  //: root_("00")
  : vectorRoot_("00")
  {}

  class Node {
  public:
    /**
     * Create a new ICTVectorState::Node with the given fields and compute the digest.
     * @param dataPrefix The data prefix.
     * @param sessionNo The sequence number.
     * @param sequenceNo The session number.
     */
    Node(const std::string& dataPrefix, int sessionNo, int sequenceNo)
    : dataPrefix_(dataPrefix),
      sessionNo_(sessionNo),
      sequenceNo_(sequenceNo)
    {
      recomputeUserDigest();
    }

    const std::string&
    getDataPrefix() const { return dataPrefix_; }

    int
    getSessionNo() const { return sessionNo_; }

    int
    getSequenceNo() const { return sequenceNo_; }

    /**
     * Get the user's digest. A user digest is the sha256 of user data name and session id
     * @return The user data name digest as a hex string.
     */
    const std::string&
    getUserDigest() const { return userDigest_; }

    /**
     * Set the sequence number and recompute the digest.
     * @param sequenceNo The new sequence number.
     */
    void
    setSequenceNo(int sequenceNo)
    {
      sequenceNo_ = sequenceNo;

      recomputeUserDigest();
    }

    /**
     * Compare shared_ptrs to Node based on dataPrefix_ and seqno_session_.
     */
    class Compare {
    public:
      bool
      operator()
        (const std::shared_ptr<const Node>& node1,
         const std::shared_ptr<const Node>& node2) const
      {
        int nameComparison = node1->dataPrefix_.compare(node2->dataPrefix_);
        if (nameComparison != 0)
          return nameComparison < 0;

        return node1->sessionNo_ < node2->sessionNo_;
      }
    };


  private:

    /**
     * comupte a digest based on dataPrefix_ and sessionNo and set userDigest_ to the hex digest.
     */
    void
    recomputeUserDigest();

    static void
    int32ToLittleEndian(uint32_t value, uint8_t* result);

    std::string dataPrefix_;
    int sessionNo_;
    int sequenceNo_;

    // digest based on session id and seq number
    std::string userDigest_;
  };

  /**
   * Update the digest tree and recompute the root digest.  If the combination
   * of dataPrefix and sessionNo already exists in the tree then update its
   * sequenceNo (only if the given sequenceNo is newer), otherwise add a new node.
   * @param dataPrefix The name prefix.
   * @param sessionNo The session number.
   * @param sequenceNo The new sequence number.
   * @return True if the digest tree is updated, false if not (because the
   * given sequenceNo is not newer than the existing sequence number).
   */
  bool
  update(const std::string& dataPrefix, int sessionNo, int sequenceNo);

  int
  find(const std::string& dataPrefix, int sessionNo) const;

  int
  find(int sessionNo) const;

  const std::string
  getSessionName(int sessionNo) const;

  size_t
  size() const { return digestNode_.size(); }

  const ICTVectorState::Node&
  get(size_t i) const { return *digestNode_[i]; }

  const std::string&
  getVectorRoot() const { return vectorRoot_; }

  int
  getDiff(const std::string& digest,
          std::vector<uint8_t>& diffNodes,
          std::vector<std::tuple<uint32_t, uint32_t>>& negativeInLocal,
          std::vector<std::tuple<uint32_t, uint32_t>>& unknownSessions,
          bool pushLocalSessions) const;
          //std::vector<uint32_t>& unknownSessions) const;
private:
  /**
   * Set vectorRoot_ to a vector of ...
   */
   void recomputeVectorRoot(); // new function to replace old hex root
  // void
  // recomputeRoot(); // deprecated


  std::vector<std::shared_ptr<ICTVectorState::Node> > digestNode_;
  std::string vectorRoot_;
  Node::Compare nodeCompare_;
};

/**
 * Convert the hex character to an integer from 0 to 15, or -1 if not a hex character.
 * @param c
 * @return
 */
static int
fromHexChar(uint8_t c)
{
  if (c >= '0' && c <= '9')
    return (int)c - (int)'0';
  else if (c >= 'A' && c <= 'F')
    return (int)c - (int)'A' + 10;
  else if (c >= 'a' && c <= 'f')
    return (int)c - (int)'a' + 10;
  else
    return -1;
}

static std::string
unescape(const std::string& str)
{
  std::ostringstream result;

  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '%' && i + 2 < str.size()) {
      int hi = fromHexChar(str[i + 1]);
      int lo = fromHexChar(str[i + 2]);

      if (hi < 0 || lo < 0)
        // Invalid hex characters, so just keep the escaped string.
        result << str[i] << str[i + 1] << str[i + 2];
      else
        result << (uint8_t)(16 * hi + lo);

      // Skip ahead past the escaped value.
      i += 2;
    }
    else
      // Just copy through.
      result << str[i];
  }

  return result.str();
}

// class Gzip {
// public:
// 	static std::string compress(const std::string& data)
// 	{
// 		namespace bio = boost::iostreams;
//
// 		std::stringstream compressed;
// 		std::stringstream origin(data);
//
// 		bio::filtering_streambuf<bio::input> out;
// 		out.push(bio::gzip_compressor(bio::gzip_params(bio::gzip::best_compression)));
// 		out.push(origin);
// 		bio::copy(out, compressed);
//
// 		return compressed.str();
// 	}
//
// 	static std::string decompress(const std::string& data)
// 	{
// 		namespace bio = boost::iostreams;
//
// 		std::stringstream compressed(data);
// 		std::stringstream decompressed;
//
// 		bio::filtering_streambuf<bio::input> out;
// 		out.push(bio::gzip_decompressor());
// 		out.push(compressed);
// 		bio::copy(out, decompressed);
//
// 		return decompressed.str();
// 	}
// };

}

#endif
