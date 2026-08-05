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

#include <aasdk/Common/Log.hpp>
#include <aasdk/Common/ModernLogger.hpp>
#include <aasdk/Transport/USBTransport.hpp>


namespace aasdk {
  namespace transport {

    USBTransport::USBTransport(boost::asio::io_service &ioService, usb::IAOAPDevice::Pointer aoapDevice)
        : Transport(ioService), aoapDevice_(std::move(aoapDevice)) {}

      bool USBTransport::isTransientReceiveError(const error::Error &e) {
      static constexpr uint32_t kLibusbTransferError = 1;
      static constexpr uint32_t kLibusbTransferTimedOut = 2;
      static constexpr uint32_t kLibusbTransferCancelled = 4294967292u;  // -4

      return e.getCode() == error::ErrorCode::USB_TRANSFER &&
             (e.getNativeCode() == kLibusbTransferError ||
              e.getNativeCode() == kLibusbTransferTimedOut ||
              e.getNativeCode() == kLibusbTransferCancelled);
    }

    void USBTransport::enqueueReceive(common::DataBuffer buffer) {
      auto usbEndpointPromise = usb::IUSBEndpoint::Promise::defer(receiveStrand_);
      usbEndpointPromise->then([this, self = this->shared_from_this()](auto bytesTransferred) {
                                 receiveRetryCount_ = 0;
                                 this->receiveHandler(bytesTransferred);
                               },
                               [this, self = this->shared_from_this(), buffer](auto e) {
                                 if (isTransientReceiveError(e) &&
                                     receiveRetryCount_ < cMaxTransientReceiveRetries) {
                                   ++receiveRetryCount_;
                                   AASDK_LOG_TRANSPORT(warning,
                                       "Transient USB receive error (retry " +
                                       std::to_string(receiveRetryCount_) + "/" +
                                       std::to_string(cMaxTransientReceiveRetries) +
                                       "): " + std::string(e.what()));
                                   this->enqueueReceive(buffer);
                                   return;
                                 }

                                 receiveRetryCount_ = 0;
                                 this->rejectReceivePromises(e);
                               });

      aoapDevice_->getInEndpoint().bulkTransfer(buffer, cReceiveTimeoutMs, std::move(usbEndpointPromise));
    }

    void USBTransport::enqueueSend(SendQueue::iterator queueElement) {
      this->doSend(queueElement, 0);
    }

    void USBTransport::doSend(SendQueue::iterator queueElement, common::Data::size_type offset) {
      auto usbEndpointPromise = usb::IUSBEndpoint::Promise::defer(sendStrand_);
      usbEndpointPromise->then(
          [this, self = this->shared_from_this(), queueElement, offset](size_t bytesTransferred) mutable {
            this->sendHandler(queueElement, offset, bytesTransferred);
          },
          [this, self = this->shared_from_this(), queueElement](const error::Error &e) mutable {
            queueElement->second->reject(e);
            sendQueue_.erase(queueElement);

            if (!sendQueue_.empty()) {
              this->doSend(sendQueue_.begin(), 0);
            }
          });

      aoapDevice_->getOutEndpoint().bulkTransfer(common::DataBuffer(queueElement->first, offset), cSendTimeoutMs,
                                                 std::move(usbEndpointPromise));
    }

    void USBTransport::sendHandler(SendQueue::iterator queueElement, common::Data::size_type offset,
                                   size_t bytesTransferred) {
      if (offset + bytesTransferred < queueElement->first.size()) {
        this->doSend(queueElement, offset + bytesTransferred);
      } else {
        queueElement->second->resolve();
        sendQueue_.erase(queueElement);

        if (!sendQueue_.empty()) {
          this->doSend(sendQueue_.begin(), 0);
        }
      }
    }

    void USBTransport::stop() {
      aoapDevice_->getInEndpoint().cancelTransfers();
      aoapDevice_->getOutEndpoint().cancelTransfers();
    }

  }
}
