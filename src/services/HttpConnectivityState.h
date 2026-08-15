#pragma once

#include <QMetaType>

enum class HttpConnectivityState
{
    Detecting,
    Online,
    Abnormal,
};

Q_DECLARE_METATYPE(HttpConnectivityState)
