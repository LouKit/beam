// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <QObject>

#include "ui/model/app_model.h"

class PushNotificationManager : public QObject
{
    Q_OBJECT

public:
    PushNotificationManager();

    Q_INVOKABLE void onCancelPopup(const QVariant& variantID);

signals:
    void showUpdateNotification(const QString&, const QString&, const QVariant&);

public slots:
    void onNewSoftwareUpdateAvailable(const beam::wallet::VersionInfo&, const ECC::uintBig& notificationID);

private:
    WalletModel& m_walletModel;
    WalletSettings& m_settings; /// TODO store last version user notified about
};
