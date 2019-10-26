// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_posix.h"

#include <utility>

#include "base/test/task_environment.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_config.h"
#include "net/dns/system_dns_config_change_notifier.h"
#include "net/dns/test_dns_config_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {

class NetworkChangeNotifierPosixTest : public testing::Test {
 public:
  NetworkChangeNotifierPosixTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    // Create a SystemDnsConfigChangeNotifier instead of letting
    // NetworkChangeNotifier create a global one, otherwise the global one will
    // hold a TaskRunner handle to |task_environment_| and crash if any
    // subsequent tests use it.
    dns_config_notifier_ = std::make_unique<SystemDnsConfigChangeNotifier>();
    notifier_.reset(new NetworkChangeNotifierPosix(
        NetworkChangeNotifier::CONNECTION_UNKNOWN,
        NetworkChangeNotifier::SUBTYPE_UNKNOWN, dns_config_notifier_.get()));
    auto dns_config_service = std::make_unique<TestDnsConfigService>();
    dns_config_service_ = dns_config_service.get();
    dns_config_notifier_->SetDnsConfigServiceForTesting(
        std::move(dns_config_service));
  }

  void FastForwardUntilIdle() {
    task_environment_.FastForwardUntilNoTasksRemain();
  }

  NetworkChangeNotifierPosix* notifier() { return notifier_.get(); }
  TestDnsConfigService* dns_config_service() { return dns_config_service_; }

 private:
  base::test::TaskEnvironment task_environment_;
  net::NetworkChangeNotifier::DisableForTest mock_notifier_disabler_;
  std::unique_ptr<SystemDnsConfigChangeNotifier> dns_config_notifier_;
  std::unique_ptr<NetworkChangeNotifierPosix> notifier_;
  TestDnsConfigService* dns_config_service_;
};

class MockIPAddressObserver : public NetworkChangeNotifier::IPAddressObserver {
 public:
  MOCK_METHOD0(OnIPAddressChanged, void());
};

TEST_F(NetworkChangeNotifierPosixTest, OnIPAddressChanged) {
  testing::StrictMock<MockIPAddressObserver> observer;
  NetworkChangeNotifier::AddIPAddressObserver(&observer);

  EXPECT_CALL(observer, OnIPAddressChanged());
  notifier()->OnIPAddressChanged();
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveIPAddressObserver(&observer);
}

class MockNetworkChangeObserver
    : public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  MOCK_METHOD1(OnNetworkChanged, void(NetworkChangeNotifier::ConnectionType));
};

TEST_F(NetworkChangeNotifierPosixTest, OnNetworkChanged) {
  testing::StrictMock<MockNetworkChangeObserver> observer;
  NetworkChangeNotifier::AddNetworkChangeObserver(&observer);

  EXPECT_CALL(observer,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer, OnNetworkChanged(NetworkChangeNotifier::CONNECTION_3G));
  notifier()->OnConnectionChanged(NetworkChangeNotifier::CONNECTION_3G);
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveNetworkChangeObserver(&observer);
}

class MockMaxBandwidthObserver
    : public NetworkChangeNotifier::MaxBandwidthObserver {
 public:
  MOCK_METHOD2(OnMaxBandwidthChanged,
               void(double, NetworkChangeNotifier::ConnectionType));
};

TEST_F(NetworkChangeNotifierPosixTest, OnMaxBandwidthChanged) {
  testing::StrictMock<MockMaxBandwidthObserver> observer;
  NetworkChangeNotifier::AddMaxBandwidthObserver(&observer);

  EXPECT_CALL(observer,
              OnMaxBandwidthChanged(3.6, NetworkChangeNotifier::CONNECTION_4G));
  notifier()->OnConnectionSubtypeChanged(NetworkChangeNotifier::CONNECTION_4G,
                                         NetworkChangeNotifier::SUBTYPE_HSPA);
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveMaxBandwidthObserver(&observer);
}

class TestDnsObserver : public NetworkChangeNotifier::DNSObserver {
 public:
  void OnDNSChanged() override { subsequent_dns_changes_++; }

  void OnInitialDNSConfigRead() override { initial_dns_changes_++; }

  int initial_dns_changes() const { return initial_dns_changes_; }
  int subsequent_dns_changes() const { return subsequent_dns_changes_; }

 private:
  int initial_dns_changes_ = 0;
  int subsequent_dns_changes_ = 0;
};

TEST_F(NetworkChangeNotifierPosixTest, OnDNSChanged) {
  TestDnsObserver observer;
  NetworkChangeNotifier::AddDNSObserver(&observer);

  DnsConfig config;
  config.nameservers = {IPEndPoint(IPAddress(1, 2, 3, 4), 233)};

  dns_config_service()->SetConfigForRefresh(config);
  notifier()->OnDNSChanged();
  FastForwardUntilIdle();
  EXPECT_EQ(1, observer.initial_dns_changes());
  EXPECT_EQ(0, observer.subsequent_dns_changes());

  config.nameservers.push_back(IPEndPoint(IPAddress(2, 3, 4, 5), 234));
  dns_config_service()->SetConfigForRefresh(config);
  notifier()->OnDNSChanged();
  FastForwardUntilIdle();
  EXPECT_EQ(1, observer.initial_dns_changes());
  EXPECT_EQ(1, observer.subsequent_dns_changes());

  config.nameservers.push_back(IPEndPoint(IPAddress(3, 4, 5, 6), 235));
  dns_config_service()->SetConfigForRefresh(config);
  notifier()->OnDNSChanged();
  FastForwardUntilIdle();
  EXPECT_EQ(1, observer.initial_dns_changes());
  EXPECT_EQ(2, observer.subsequent_dns_changes());

  NetworkChangeNotifier::RemoveDNSObserver(&observer);
}

}  // namespace net
