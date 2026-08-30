// SPDX-License-Identifier: Apache-2.0
//
// Hand-written moc output for kfclient::ApiClient.
//
// Generated to match Qt 6 (moc revision 12) so the project can build without
// running moc.exe (blocked in this environment by policy). The metaobject
// table contains only signals; slots are connected via the function-pointer
// overload of QObject::connect and therefore need not be registered.

#if 0
#pragma qt_no_skip_meta_object_directive
#endif

#include "ApiClient.h"

#include <cstring>
#include <QtCore/qmetatype.h>

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_kfclient__ApiClient_t {
    const uint offsetsAndSizes[70];
    char stringdata0[431];
};
#define QT_STRINGIFY_COMPONENT(x) x,
static const qt_meta_stringdata_kfclient__ApiClient_t qt_meta_stringdata_kfclient__ApiClient = {
    {
   0,   9,  10,   9,  20,  12,  33,  11,  45,   7,  53,   7,  61,  16,  78,   7,  86,   9,  96,  10, 107,   4, 112,   5, 118,  13, 132,  19, 152,   5, 158,  13, 172,  19, 192,   5, 198,  13, 212,  19, 232,   5, 238,  16, 255,  22, 278,   8, 287,  13, 301,  19, 321,   5, 327,  19, 347,  25, 373,   5, 379,  21, 401,   8, 410,   7, 418,  10, 429,   0
    },
    {
"ApiClient\0connected\0disconnected\0socketError\0QString\0message\0responseReceived\0quint64\0requestId\0QJsonValue\0data\0error\0quoteReceived\0kfclient::QuoteInfo\0quote\0orderReceived\0kfclient::OrderInfo\0order\0tradeReceived\0kfclient::TradeInfo\0trade\0positionReceived\0kfclient::PositionInfo\0position\0assetReceived\0kfclient::AssetInfo\0asset\0brokerStateReceived\0kfclient::BrokerStateInfo\0state\0genericBinaryReceived\0typeName\0summary\0logMessage\0\0"
    }
};
#undef QT_STRINGIFY_COMPONENT
static const uint qt_meta_data_kfclient__ApiClient[] = {
 // content:
       12,       // revision
        0,       // classname
        0,    0, // classinfo
  12,   14, // methods: count, offset
        0,    0, // properties
        0,    0, // enums/sets
        0,    0, // constructors
        0,       // flags
12,       // signalCount
 // methods: name, argc, parameters, tag, flags, metatypeoffset
  12,   0,   0,   0,  12,  14,   0,   0,   0,   0,   0,   0,   0,  12,   1,   0,  86,  34,   6,   0,   2,   0,  86,  34,   6,   0,   3,   1,  86,  34,   6,   0,   6,   3,  88,  34,   6,   0,  12,   1,  94,  34,   6,   0,  15,   1,  96,  34,   6,   0,  18,   1,  98,  34,   6,   0,  21,   1, 100,  34,   6,   0,  24,   1, 102,  34,   6,   0,  27,   1, 104,  34,   6,   0,  30,   2, 106,  34,   6,   0,  33,   1, 110,  34,   6,   0,   4,   5,   7,   8,   9,  10,   4,  11,  13,  14,  16,  17,  19,  20,  22,  23,  25,  26,  28,  29,   4,  31,   4,  32,   4,   5,   0
};
const QMetaObject kfclient::ApiClient::staticMetaObject = {{
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_kfclient__ApiClient.offsetsAndSizes,
    qt_meta_data_kfclient__ApiClient,
    qt_static_metacall,
    nullptr,
    nullptr
}};
const QMetaObject *kfclient::ApiClient::metaObject() const
{
    return &staticMetaObject;
}
void *kfclient::ApiClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_kfclient__ApiClient.stringdata0))
        return static_cast<void *>(this);
    return QObject::qt_metacast(_clname);
}
int kfclient::ApiClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<int *>(_a[0]) = -1;
        _id -= 12;
    }
    return _id;
}
void kfclient::ApiClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (kfclient::ApiClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::connected))
                { *result = 0; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::disconnected))
                { *result = 1; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const QString &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::socketError))
                { *result = 2; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(quint64, const QJsonValue &, const QString &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::responseReceived))
                { *result = 3; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::QuoteInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::quoteReceived))
                { *result = 4; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::OrderInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::orderReceived))
                { *result = 5; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::TradeInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::tradeReceived))
                { *result = 6; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::PositionInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::positionReceived))
                { *result = 7; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::AssetInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::assetReceived))
                { *result = 8; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const kfclient::BrokerStateInfo &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::brokerStateReceived))
                { *result = 9; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const QString &, const QString &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::genericBinaryReceived))
                { *result = 10; return; }
        }
        {
            using _t = void (kfclient::ApiClient::*)(const QString &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::ApiClient::logMessage))
                { *result = 11; return; }
        }
    }
}

void kfclient::ApiClient::connected()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

void kfclient::ApiClient::disconnected()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

void kfclient::ApiClient::socketError(const QString & message)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&message)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

void kfclient::ApiClient::responseReceived(quint64 requestId, const QJsonValue & data, const QString & error)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&requestId)), const_cast<void *>(reinterpret_cast<const void *>(&data)), const_cast<void *>(reinterpret_cast<const void *>(&error)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

void kfclient::ApiClient::quoteReceived(const kfclient::QuoteInfo & quote)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&quote)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

void kfclient::ApiClient::orderReceived(const kfclient::OrderInfo & order)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&order)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

void kfclient::ApiClient::tradeReceived(const kfclient::TradeInfo & trade)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&trade)) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

void kfclient::ApiClient::positionReceived(const kfclient::PositionInfo & position)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&position)) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

void kfclient::ApiClient::assetReceived(const kfclient::AssetInfo & asset)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&asset)) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

void kfclient::ApiClient::brokerStateReceived(const kfclient::BrokerStateInfo & state)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&state)) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

void kfclient::ApiClient::genericBinaryReceived(const QString & typeName, const QString & summary)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&typeName)), const_cast<void *>(reinterpret_cast<const void *>(&summary)) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

void kfclient::ApiClient::logMessage(const QString & message)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&message)) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

QT_WARNING_POP
QT_END_MOC_NAMESPACE
