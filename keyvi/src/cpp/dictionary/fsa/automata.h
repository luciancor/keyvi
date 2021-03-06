/* * keyvi - A key value store.
 *
 * Copyright 2015 Hendrik Muhs<hendrik.muhs@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * automata.h
 *
 *  Created on: May 12, 2014
 *      Author: hendrik
 */

#ifndef AUTOMATA_H_
#define AUTOMATA_H_

#include <sys/mman.h>
#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include "dictionary/fsa/internal/constants.h"
#include "dictionary/fsa/internal/value_store_factory.h"
#include "dictionary/fsa/internal/serialization_utils.h"
#include "dictionary/util/vint.h"
#include "dictionary/util/endian.h"
#include "dictionary/fsa/internal/intrinsics.h"

//#define ENABLE_TRACING
#include "dictionary/util/trace.h"

namespace keyvi {
namespace dictionary {
namespace fsa {


class Automata
final {

   public:
    Automata(const char * filename, bool load_lazy=false) {
      std::ifstream in_stream(filename, std::ios::binary);

      if (!in_stream.good()) {
        throw std::invalid_argument("file not found");
      }

      char magic[8];
      in_stream.read(magic, sizeof(magic));

      // check magic
      if (std::strncmp(magic, "KEYVIFSA", 8)){
        throw std::invalid_argument("not a keyvi file");
      }

      automata_properties_ = internal::SerializationUtils::ReadJsonRecord(
          in_stream);
      sparse_array_properties_ = internal::SerializationUtils::ReadJsonRecord(
          in_stream);

      compact_size_ = sparse_array_properties_.get<uint32_t>("version", 1) == 2;
      size_t bucket_size = compact_size_ ? sizeof(uint16_t) : sizeof(uint32_t);

      size_t offset = in_stream.tellg();

      file_mapping_ = new boost::interprocess::file_mapping(
          filename, boost::interprocess::read_only);
      size_t array_size = sparse_array_properties_.get<size_t>("size");

      in_stream.seekg(offset + array_size + bucket_size * array_size - 1);

      // check for file truncation
      if (in_stream.peek() == EOF) {
        throw std::invalid_argument("file is corrupt(truncated)");
      }

      boost::interprocess::map_options_t map_options = boost::interprocess::default_map_options | MAP_HUGETLB;

      if (!load_lazy) {
        map_options |= MAP_POPULATE;
      }

      TRACE("labels start offset: %d", offset);
      labels_region_ = new boost::interprocess::mapped_region(
          *file_mapping_, boost::interprocess::read_only, offset, array_size, 0, map_options);

      TRACE("transitions start offset: %d", offset + array_size);
      transitions_region_ = new boost::interprocess::mapped_region(
          *file_mapping_, boost::interprocess::read_only, offset + array_size,
          bucket_size * array_size, 0, map_options);

      TRACE("full file size %zu", offset + array_size + bucket_size * array_size);

      labels_ = (unsigned char*) labels_region_->get_address();
      transitions_ = (uint32_t*) transitions_region_->get_address();
      transitions_compact_ = (uint16_t*) transitions_region_->get_address();

      // forward 1 position
      in_stream.get();
      TRACE("value store position %zu", in_stream.tellg());

      // initialize value store
      internal::value_store_t value_store_type =
          static_cast<internal::value_store_t>(automata_properties_.get<int>(
              "value_store_type"));
      value_store_reader_ = internal::ValueStoreFactory::MakeReader(value_store_type, in_stream, file_mapping_);

      in_stream.close();
    }

    ~Automata() {
      delete value_store_reader_;
      delete file_mapping_;
      delete labels_region_;
      delete transitions_region_;
    }

    Automata() = delete;
    Automata& operator=(Automata const&) = delete;
    Automata(const Automata& that) = delete;

    /**
     * Get the start(root) stage of the FSA
     *
     * @return index of root state.
     */
    uint32_t GetStartState() const {
      return automata_properties_.get<uint32_t>("start_state");
    }

    uint32_t GetNumberOfKeys() const {
      return automata_properties_.get<uint32_t>("number_of_keys");
    }

    uint32_t TryWalkTransition(uint32_t starting_state, unsigned char c) const {
      if (labels_[starting_state + c] == c) {
        return ResolvePointer(starting_state, c);
      }
      return 0;
    }

    /**
     * Get the outgoing states of state quickly in 1 step.
     *
     * @param starting_state The state
     * @param outgoing_states a vector given by reference to put n the outgoing states
     * @param outgoing_symbols a vector given by reference to put in the outgoing symbols (labels)
     */
    void GetOutGoingTransitions(uint32_t starting_state, std::vector<uint32_t>& outgoing_states,
                                std::vector<unsigned char>& outgoing_symbols) const {
      outgoing_states.clear();
      outgoing_symbols.clear();

      std::vector<uint32_t> outgoing_states2;
      std::vector<unsigned char> outgoing_symbols2;

#if defined(KEYVI_SSE42)
      // Optimized version using SSE4.2, see http://www.strchr.com/strcmp_and_strlen_using_sse_4.2

      __m128i* labels_as_m128 = (__m128i *) (labels_ + starting_state);
      __m128i* mask_as_m128 = (__m128i *) (OUTGOING_TRANSITIONS_MASK);
      unsigned char symbol = 0;

      // check 16 bytes at a time
      for (int offset = 0; offset < 16; ++offset) {
        __m128i mask = _mm_cmpestrm(_mm_loadu_si128(labels_as_m128), 16,
                            _mm_loadu_si128(mask_as_m128), 16,
                            _SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_EACH|_SIDD_MASKED_POSITIVE_POLARITY|_SIDD_BIT_MASK);

        uint64_t mask_int = ((uint64_t*)&mask)[0];
        TRACE ("Bitmask %d", mask_int);

        if (mask_int != 0) {
          if (offset == 0) {
            // in this case we have to ignore the first bit, so start counting from 1
            mask_int = mask_int >> 1;
            for (auto i=1; i<16; ++i) {
              if ((mask_int & 1) == 1) {
                TRACE("push symbol+%d", symbol + i);
                outgoing_symbols.push_back(symbol + i);
                outgoing_states.push_back(ResolvePointer(starting_state, symbol + i));
              }
              mask_int = mask_int >> 1;
            }
          } else {
            for (auto i=0; i<16; ++i) {
              if ((mask_int & 1) == 1) {
                TRACE("push symbol+%d", symbol + i);
                outgoing_symbols.push_back(symbol + i);
                outgoing_states.push_back(ResolvePointer(starting_state, symbol + i));
              }
              mask_int = mask_int >> 1;
            }
          }
        }

        ++labels_as_m128;
        ++mask_as_m128;
        symbol +=16;
      }
#else
      uint64_t* labels_as_ll = (unsigned long int *) (labels_ + starting_state);
      uint64_t* mask_as_ll = (unsigned long int *) (OUTGOING_TRANSITIONS_MASK);
      unsigned char symbol = 0;

      // check 8 bytes at a time
      for (int offset = 0; offset < 32; ++offset) {

        uint64_t xor_labels_with_mask = *labels_as_ll^*mask_as_ll;

        if (((xor_labels_with_mask & 0x00000000000000ffULL) == 0) && offset > 0){
          outgoing_symbols.push_back(symbol);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol));
        }
        if ((xor_labels_with_mask & 0x000000000000ff00ULL)== 0){
          outgoing_symbols.push_back(symbol + 1);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 1 ));
        }
        if ((xor_labels_with_mask & 0x0000000000ff0000ULL)== 0){
          outgoing_symbols.push_back(symbol + 2);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 2 ));
        }
        if ((xor_labels_with_mask & 0x00000000ff000000ULL)== 0){
          outgoing_symbols.push_back(symbol + 3);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 3 ));
        }
        if ((xor_labels_with_mask & 0x000000ff00000000ULL)== 0){
          outgoing_symbols.push_back(symbol + 4);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 4 ));
        }
        if ((xor_labels_with_mask & 0x0000ff0000000000ULL)== 0){
          outgoing_symbols.push_back(symbol + 5);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 5 ));
        }
        if ((xor_labels_with_mask & 0x00ff000000000000ULL)== 0){
          outgoing_symbols.push_back(symbol + 6);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 6 ));
        }
        if ((xor_labels_with_mask & 0xff00000000000000ULL)== 0){
          outgoing_symbols.push_back(symbol + 7);
          outgoing_states.push_back(ResolvePointer(starting_state, symbol + 7 ));
        }

        ++labels_as_ll;
        ++mask_as_ll;
        symbol +=8;
      }
#endif

      return;
    }

    bool IsFinalState(uint32_t state_to_check) const {

      if (labels_[state_to_check + FINAL_OFFSET_TRANSITION] == FINAL_OFFSET_CODE) {
        return true;
      }
      return false;
    }

    uint64_t GetStateValue(uint32_t state) const {
      if (!compact_size_) {
        return be32toh(transitions_[state + FINAL_OFFSET_TRANSITION]);
      }

      // compact mode:
      return util::decodeVarshort(transitions_compact_ + state + FINAL_OFFSET_TRANSITION);
    }

    uint32_t GetWeightValue(uint32_t state) const {
      if (!compact_size_) {
        if (labels_[state + INNER_WEIGHT_TRANSITION] != 0) {
          return 0;
        }

        return be32toh(transitions_[state + INNER_WEIGHT_TRANSITION]);
      }

      if (labels_[state + INNER_WEIGHT_TRANSITION_COMPACT] != 0) {
        return 0;
      }

      return (transitions_compact_[state + INNER_WEIGHT_TRANSITION_COMPACT]);
    }

    internal::IValueStoreReader::attributes_t GetValueAsAttributeVector(uint64_t state_value) const {
      return value_store_reader_->GetValueAsAttributeVector(state_value);
    }

    std::string GetValueAsString(uint64_t state_value) const {
      return value_store_reader_->GetValueAsString(state_value);
    }

    std::string GetRawValueAsString(uint64_t state_value) const {
      return value_store_reader_->GetRawValueAsString(state_value);
    }

    std::string GetStatistics() const {
      std::ostringstream buf;
      buf << "General" << std::endl;
      boost::property_tree::write_json (buf, automata_properties_, false);
      buf << std::endl << "Persistence" << std::endl;
      boost::property_tree::write_json (buf, sparse_array_properties_, false);
      buf << std::endl << "Value Store" << std::endl;
      buf << value_store_reader_->GetStatistics();
      return buf.str();
    }

    boost::property_tree::ptree GetManifest() const {
      return automata_properties_.get_child("manifest", boost::property_tree::ptree());
    }

    std::string GetManifestAsString() const {
      std::ostringstream buf;

      const boost::property_tree::ptree &manifest = automata_properties_.get_child("manifest", boost::property_tree::ptree());
      boost::property_tree::write_json (buf, manifest, false);

      return buf.str();
    }

   private:
    boost::property_tree::ptree automata_properties_;
    boost::property_tree::ptree sparse_array_properties_;
    internal::IValueStoreReader* value_store_reader_;
    boost::interprocess::file_mapping* file_mapping_;
    boost::interprocess::mapped_region* labels_region_;
    boost::interprocess::mapped_region* transitions_region_;
    unsigned char* labels_;
    uint32_t* transitions_;
    uint16_t* transitions_compact_;
    bool compact_size_;

    inline uint32_t ResolvePointer(uint32_t starting_state, unsigned char c) const {
      if (!compact_size_) {
        return be32toh(transitions_[starting_state + c]);
      }

      uint16_t pt = le16toh(transitions_compact_[starting_state + c]);
      uint32_t resolved_ptr;

      if ((pt & 0xC000) == 0xC000) {
        TRACE("Compact Transition uint16 absolute");

        resolved_ptr = pt & 0x3FFF;
        TRACE("Compact Transition after resolve %d", resolved_ptr);
        return resolved_ptr;
      }

      if (pt & 0x8000){
        TRACE("Compact Transition overflow %d (from %d) starting from %d", pt, starting_state + c, starting_state);

        // clear the first bit
        pt &= 0x7FFF;
        size_t overflow_bucket;
        TRACE("Compact Transition overflow bucket %d", pt);

        overflow_bucket = (pt >> 4) + starting_state + c - 512;

        TRACE("Compact Transition found overflow bucket %d", overflow_bucket);

        resolved_ptr = util::decodeVarshort(transitions_compact_ + overflow_bucket);
        resolved_ptr = (resolved_ptr << 3) + (pt & 0x7);

        if (pt & 0x8){
          // relative coding
          resolved_ptr = (starting_state + c) - resolved_ptr + 512;
        }

      } else {
        TRACE("Compact Transition uint16 transition %d", pt);
          resolved_ptr = (starting_state + c) - pt + 512;

      }

      TRACE("Compact Transition after resolve %d", resolved_ptr);
      return resolved_ptr;
    }
  };

  // shared pointer
  typedef std::shared_ptr<Automata> automata_t;

  } /* namespace fsa */
  } /* namespace dictionary */
  } /* namespace keyvi */

#endif /* AUTOMATA_H_ */
