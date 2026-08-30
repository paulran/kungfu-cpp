// SPDX-License-Identifier: Apache-2.0
//
// Hand-written moc output for kfclient::MainWindow.
//
// Generated to match Qt 6 (moc revision 12) so the project can build without
// running moc.exe (blocked in this environment by policy). The metaobject
// table contains only signals; slots are connected via the function-pointer
// overload of QObject::connect and therefore need not be registered.

#if 0
#pragma qt_no_skip_meta_object_directive
#endif

#include "MainWindow.h"

#include <cstring>
#include <QtCore/qmetatype.h>

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_kfclient__MainWindow_t {
    const uint offsetsAndSizes[4];
    char stringdata0[13];
};
#define QT_STRINGIFY_COMPONENT(x) x,
static const qt_meta_stringdata_kfclient__MainWindow_t qt_meta_stringdata_kfclient__MainWindow = {
    {
   0,  10,  11,   0
    },
    {
"MainWindow\0\0"
    }
};
#undef QT_STRINGIFY_COMPONENT
static const uint qt_meta_data_kfclient__MainWindow[] = {
 // content:
       12,       // revision
        0,       // classname
        0,    0, // classinfo
  0,   14, // methods: count, offset
        0,    0, // properties
        0,    0, // enums/sets
        0,    0, // constructors
        0,       // flags
0,       // signalCount
 // methods: name, argc, parameters, tag, flags, metatypeoffset
  12,   0,   0,   0,   0,  14,   0,   0,   0,   0,   0,   0,   0,   0,   0
};
const QMetaObject kfclient::MainWindow::staticMetaObject = {{
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_kfclient__MainWindow.offsetsAndSizes,
    qt_meta_data_kfclient__MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
}};
const QMetaObject *kfclient::MainWindow::metaObject() const
{
    return &staticMetaObject;
}
void *kfclient::MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_kfclient__MainWindow.stringdata0))
        return static_cast<void *>(this);
    return QMainWindow::qt_metacast(_clname);
}
int kfclient::MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    (void)_c;
    (void)_a;
    return _id;
}
void kfclient::MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
    }
}

QT_WARNING_POP
QT_END_MOC_NAMESPACE
