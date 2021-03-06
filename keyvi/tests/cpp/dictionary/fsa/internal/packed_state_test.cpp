//
// keyvi - A key value store.
//
// Copyright 2015 Hendrik Muhs<hendrik.muhs@gmail.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

/*
 * packed_state_test.cpp
 *
 *  Created on: May 2, 2014
 *      Author: hendrik
 */

#include <boost/test/unit_test.hpp>

#include "dictionary/fsa/internal/packed_state.h"

namespace keyvi {
namespace dictionary {
namespace fsa {
namespace internal {

// The name of the suite must be a different name to your class
BOOST_AUTO_TEST_SUITE( PackedStateTests )

BOOST_AUTO_TEST_CASE( size )
{
  BOOST_CHECK(sizeof(PackedState) == 12);
}

BOOST_AUTO_TEST_CASE( numOutgoingAndPrivateUse )
{
  PackedState p1 {0, 0, 1};

  BOOST_CHECK(p1.GetNumberOfOutgoingTransitions() == 1);
  BOOST_CHECK(p1.GetCookie() == 0);
  p1.SetCookie(25);
  BOOST_CHECK(p1.GetNumberOfOutgoingTransitions() == 1);
  BOOST_CHECK(p1.GetCookie() == 25);
  p1.SetCookie(6948);
  BOOST_CHECK(p1.GetNumberOfOutgoingTransitions() == 1);
  BOOST_CHECK(p1.GetCookie() == 6948);
  p1.SetCookie(0);
  BOOST_CHECK(p1.GetNumberOfOutgoingTransitions() == 1);
  BOOST_CHECK(p1.GetCookie() == 0);

  PackedState p2 = {0, 0, 257};
  BOOST_CHECK(p2.GetNumberOfOutgoingTransitions() == 257);
  p2.SetCookie(6948);
  BOOST_CHECK(p2.GetNumberOfOutgoingTransitions() == 257);
  BOOST_CHECK(p2.GetCookie() == 6948);

  p2.SetCookie(PackedState::GetMaxCookieSize());
  BOOST_CHECK(p2.GetNumberOfOutgoingTransitions() == 257);
  BOOST_CHECK(p2.GetCookie() == PackedState::GetMaxCookieSize());
}


BOOST_AUTO_TEST_SUITE_END()

} /* namespace internal */
} /* namespace fsa */
} /* namespace dictionary */
} /* namespace keyvi */
