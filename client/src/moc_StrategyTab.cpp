// SPDX-License-Identifier: Apache-2.0
//
// Hand-written moc output for kfclient::StrategyTab.
//
// Generated to match Qt 6 (moc revision 12) so the project can build without
// running moc.exe (blocked in this environment by policy). The metaobject
// table contains only signals; slots are connected via the function-pointer
// overload of QObject::connect and therefore need not be registered.

#if 0
#pragma qt_no_skip_meta_object_directive
#endif

#include "StrategyTab.h"

#include <cstring>
#include <QtCore/qmetatype.h>

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_kfclient__StrategyTab_t {
    const uint offsetsAndSizes[10];
    char stringdata0[37];
};
#define QT_STRINGIFY_COMPONENT(x) x,
static const qt_meta_stringdata_kfclient__StrategyTab_t qt_meta_stringdata_kfclient__StrategyTab = {
    {
   0,  11,  12,  10,  23,   7,  31,   3,  35,   0
    },
    {
"StrategyTab\0logMessage\0QString\0msg\0\0"
    }
};
#undef QT_STRINGIFY_COMPONENT
static const uint qt_meta_data_kfclient__StrategyTab[] = {
 // content:
       12,       // revision
        0,       // classname
        0,    0, // classinfo
  1,   14, // methods: count, offset
        0,    0, // properties
        0,    0, // enums/sets
        0,    0, // constructors
        0,       // flags
1,       // signalCount
 // methods: name, argc, parameters, tag, flags, metatypeoffset
  12,   0,   0,   0,   1,  14,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,  20,   4,   6,   0,   2,   3,   0
};
const QMetaObject kfclient::StrategyTab::staticMetaObject = {{
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_kfclient__StrategyTab.offsetsAndSizes,
    qt_meta_data_kfclient__StrategyTab,
    qt_static_metacall,
    nullptr,
    nullptr
}};
const QMetaObject *kfclient::StrategyTab::metaObject() const
{
    return &staticMetaObject;
}
void *kfclient::StrategyTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_kfclient__StrategyTab.stringdata0))
        return static_cast<void *>(this);
    return QWidget::qt_metacast(_clname);
}
int kfclient::StrategyTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<int *>(_a[0]) = -1;
        _id -= 1;
    }
    return _id;
}
void kfclient::StrategyTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (kfclient::StrategyTab::*)(const QString &);
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&kfclient::StrategyTab::logMessage))
                { *result = 0; return; }
        }
    }
}

void kfclient::StrategyTab::logMessage(const QString & msg)
{
    void *_a[] = { nullptr, const_cast<void *>(reinterpret_cast<const void *>(&msg)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

QT_WARNING_POP
QT_END_MOC_NAMESPACE
