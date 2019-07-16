//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <casinocoin/beast/insight/Base.h>
#include <casinocoin/beast/insight/BaseImpl.h>
#include <casinocoin/beast/insight/CounterImpl.h>
#include <casinocoin/beast/insight/EventImpl.h>
#include <casinocoin/beast/insight/GaugeImpl.h>
#include <casinocoin/beast/insight/MeterImpl.h>

namespace beast {
namespace insight {

Base::~Base () = default;

BaseImpl::~BaseImpl () = default;

CounterImpl::~CounterImpl () = default;

EventImpl::~EventImpl () = default;

GaugeImpl::~GaugeImpl () = default;

MeterImpl::~MeterImpl () = default;

}
}
