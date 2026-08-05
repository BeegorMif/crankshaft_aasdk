// This file is part of aasdk library project.
// Copyright (C) 2018 f1x.studio (Michal Szwaj)
// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// aasdk is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// aasdk is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with aasdk. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <boost/asio.hpp>
#include <aasdk/Transport/Transport.hpp>
#include <aasdk/USB/IAOAPDevice.hpp>


namespace aasdk {
  namespace transport {

    class USBTransport : public Transport {
    public:
      USBTransport(boost::asio::io_service &ioService, usb::IAOAPDevice::Pointer aoapDevice);

      void stop() override;

    private:
      void enqueueReceive(common::DataBuffer buffer) override;

      void enqueueSend(SendQueue::iterator queueElement) override;

      void doSend(SendQueue::iterator queueElement, common::Data::size_type offset);

      void sendHandler(SendQueue::iterator queueElement, common::Data::size_type offset, size_t bytesTransferred);

      // Returns true for libusb transfer errors that are transient at the
      // single-transfer level (generic error, timeout, or a transfer
      // cancelled by a concurrent libusb reset) and therefore safe to retry
      // in place without tearing anything down. LIBUSB_TRANSFER_NO_DEVICE is
      // deliberately excluded: it means the underlying device handle is gone,
      // which a same-handle retry cannot fix. That case is left to propagate
      // immediately so the owning service (which controls device discovery
      // and re-negotiation) can decide whether to start a fresh session.
      static bool isTransientReceiveError(const error::Error &e);

      usb::IAOAPDevice::Pointer aoapDevice_;

      uint32_t receiveRetryCount_ = 0;

      static constexpr uint32_t cSendTimeoutMs = 10000;
      static constexpr uint32_t cReceiveTimeoutMs = 0;
      static constexpr uint32_t cMaxTransientReceiveRetries = 3;

    };

  }
}
