# Hand-written moc generator for the kf_qt_client (Qt6, revision 12).
#
# The build environment blocks moc.exe by an AppLocker hash rule, so we
# generate the moc output ourselves and check it in. Only signals are placed
# in the metaobject table: every connect() in the client uses the
# function-pointer overload, which resolves the signal via IndexOfMethod
# (PMF comparison) and never needs slot entries, argument metatypes, or
# string-based lookup. metaTypes is therefore left null (and metatypeoffset
# 0) which Qt6 tolerates because parameterMetaType()/returnMetaType() are
# only reached by string-based connect / invokeMethod, neither of which the
# client uses.

$ErrorActionPreference = 'Stop'

# ---- class metadata -------------------------------------------------------
# Each class: ns, cls, parent (C++ base class name), parentMeta (X::staticMetaObject),
# and signals (each: name + params as list of @{t=pmf-type; s=string-table-type; n=name}).

$ApiClient = @{
  ns='kfclient'; cls='ApiClient'; parent='QObject'; parentMeta='QObject::staticMetaObject'
  signals=@(
    @{name='connected';    params=@()},
    @{name='disconnected'; params=@()},
    @{name='socketError';  params=@(@{t='const QString &';              s='QString';                n='message'})},
    @{name='responseReceived'; params=@(
        @{t='quint64';            s='quint64';                 n='requestId'},
        @{t='const QJsonValue &'; s='QJsonValue';              n='data'},
        @{t='const QString &';    s='QString';                 n='error'}
      )},
    @{name='quoteReceived'; params=@(@{t='const kfclient::QuoteInfo &';         s='kfclient::QuoteInfo';         n='quote'})},
    @{name='orderReceived'; params=@(@{t='const kfclient::OrderInfo &';         s='kfclient::OrderInfo';         n='order'})},
    @{name='tradeReceived'; params=@(@{t='const kfclient::TradeInfo &';         s='kfclient::TradeInfo';         n='trade'})},
    @{name='positionReceived'; params=@(@{t='const kfclient::PositionInfo &';  s='kfclient::PositionInfo';      n='position'})},
    @{name='assetReceived'; params=@(@{t='const kfclient::AssetInfo &';         s='kfclient::AssetInfo';         n='asset'})},
    @{name='brokerStateReceived'; params=@(@{t='const kfclient::BrokerStateInfo &'; s='kfclient::BrokerStateInfo'; n='state'})},
    @{name='genericBinaryReceived'; params=@(
        @{t='const QString &'; s='QString'; n='typeName'},
        @{t='const QString &'; s='QString'; n='summary'}
      )},
    @{name='logMessage'; params=@(@{t='const QString &'; s='QString'; n='message'})}
  )
}

$StrategyTab = @{
  ns='kfclient'; cls='StrategyTab'; parent='QWidget'; parentMeta='QWidget::staticMetaObject'
  signals=@( @{name='logMessage'; params=@(@{t='const QString &'; s='QString'; n='msg'})} )
}
$TradingTab = @{
  ns='kfclient'; cls='TradingTab'; parent='QWidget'; parentMeta='QWidget::staticMetaObject'
  signals=@( @{name='logMessage'; params=@(@{t='const QString &'; s='QString'; n='msg'})} )
}
$MarketDataTab = @{
  ns='kfclient'; cls='MarketDataTab'; parent='QWidget'; parentMeta='QWidget::staticMetaObject'
  signals=@( @{name='logMessage'; params=@(@{t='const QString &'; s='QString'; n='msg'})} )
}
$PositionAssetTab = @{
  ns='kfclient'; cls='PositionAssetTab'; parent='QWidget'; parentMeta='QWidget::staticMetaObject'
  signals=@( @{name='logMessage'; params=@(@{t='const QString &'; s='QString'; n='msg'})} )
}
$MainWindow = @{
  ns='kfclient'; cls='MainWindow'; parent='QMainWindow'; parentMeta='QMainWindow::staticMetaObject'
  signals=@()
}

$classes = $ApiClient,$StrategyTab,$TradingTab,$MarketDataTab,$PositionAssetTab,$MainWindow

# ---- helpers --------------------------------------------------------------

function Unique-Strings($class) {
  # Build the ordered, deduplicated list of strings.
  # Order: classname, then for each signal: name, then per param (type, name).
  # A trailing empty string is used for the tag of every method.
  $list = New-Object System.Collections.Generic.List[string]
  $seen = @{}
  function Add($s) {
    if (-not $seen.ContainsKey($s)) { $seen[$s]=$true; $list.Add($s) | Out-Null }
  }
  Add $class.cls
  foreach ($sig in $class.signals) {
    Add $sig.name
    foreach ($p in $sig.params) { Add $p.s; Add $p.n }
  }
  Add ''   # empty tag string
  return ,$list
}

function Esc-Lit($s) {
  # Escape a single string for use inside a C++ string literal.
  $r = $s -replace '\\','\\' -replace '"','\"'
  return $r
}

function Gen-File($class) {
  $key = $class.ns + '__' + $class.cls
  $fq  = $class.ns + '::' + $class.cls
  $strings = Unique-Strings $class
  $N = $strings.Count

  # offsetsAndSizes + stringdata0
  $offArr = New-Object System.Collections.Generic.List[uint32]
  $lit = New-Object System.Text.StringBuilder
  $pos = 0
  for ($i=0; $i -lt $N; $i++) {
    $s = $strings[$i]
    $offArr.Add([uint32]$pos) | Out-Null
    $offArr.Add([uint32]$s.Length) | Out-Null
    [void]$lit.Append((Esc-Lit $s))
    [void]$lit.Append('\0')
    $pos += $s.Length + 1
  }
  $S = $pos                       # explicit bytes (each string + its null)
  $sdSize = $S + 1               # +1 for the literal's implicit trailing null
  $stringdata0Lit = '"' + $lit.ToString() + '"'

  $offCpp = (($offArr | ForEach-Object { '{0,4}' -f [int]$_ }) -join ',')

  # method table + parameter blocks
  $methodCount = $class.signals.Count
  $signalCount = $methodCount
  $methodsOffset = 14
  $paramBase = $methodsOffset + 6 * $methodCount

  $data = New-Object System.Collections.Generic.List[uint32]
  # header (14)
  $data.Add(12)            # revision
  $data.Add(0)             # classname string index
  $data.Add(0); $data.Add(0)   # classinfo
  $data.Add($methodCount); $data.Add($methodsOffset)
  $data.Add(0); $data.Add(0)   # properties
  $data.Add(0); $data.Add(0)   # enums
  $data.Add(0); $data.Add(0)   # constructors
  $data.Add(0)             # flags
  $data.Add($signalCount) # signalCount

  # string index lookup
  $idxOf = @{}
  for ($i=0; $i -lt $N; $i++) { $idxOf[$strings[$i]] = $i }
  $emptyIdx = $idxOf['']

  # We'll append method entries first, then their parameter blocks.
  $methodEntries = New-Object System.Collections.Generic.List[object] # each: array of 6 values (params placeholder fixed later)
  $paramBlocks   = New-Object System.Collections.Generic.List[uint32]
  $curParamOff   = $paramBase

  foreach ($sig in $class.signals) {
    $nameIdx  = $idxOf[$sig.name]
    $argc     = $sig.params.Count
    $tagIdx   = $emptyIdx
    $flags    = 0x06   # AccessPublic | MethodSignal (signals: == public in Qt6)
    $mtOff    = 0      # metaTypes is null; metatypeoffset unused
    # parameter block
    if ($argc -gt 0) {
      $paramOffset = $curParamOff
      foreach ($p in $sig.params) {
        $paramBlocks.Add([uint32]$idxOf[$p.s]) | Out-Null
        $paramBlocks.Add([uint32]$idxOf[$p.n]) | Out-Null
        $curParamOff += 2
      }
    } else {
      $paramOffset = $curParamOff   # points at next block (unused, argc==0)
    }
    $data.Add([uint32]$nameIdx)
    $data.Add([uint32]$argc)
    $data.Add([uint32]$paramOffset)
    $data.Add([uint32]$tagIdx)
    $data.Add([uint32]$flags)
    $data.Add([uint32]$mtOff)
  }
  # append parameter blocks
  foreach ($v in $paramBlocks) { $data.Add($v) | Out-Null }
  # end-of-data marker
  $data.Add(0) | Out-Null

  $dataCpp = (($data | ForEach-Object { '{0,4}' -f [int]$_ }) -join ',')

  # ---- build signal bodies + IndexOfMethod blocks ----
  $sigBodies = New-Object System.Text.StringBuilder
  $indexBlocks = New-Object System.Text.StringBuilder

  for ($i=0; $i -lt $class.signals.Count; $i++) {
    $sig = $class.signals[$i]
    $argc = $sig.params.Count
    # pmf signature types and formal params
    $pmfTypes = ($sig.params | ForEach-Object { $_.t }) -join ', '
    $formals  = ($sig.params | ForEach-Object { '{0} {1}' -f $_.t, $_.n }) -join ', '

    # signal body
    [void]$sigBodies.AppendLine()
    [void]$sigBodies.Append("void $fq::$($sig.name)($formals)`n")
    [void]$sigBodies.Append("{`n")
    if ($argc -eq 0) {
      [void]$sigBodies.Append("    QMetaObject::activate(this, &staticMetaObject, $i, nullptr);`n")
    } else {
      [void]$sigBodies.Append("    void *_a[] = { nullptr")
      foreach ($p in $sig.params) {
        [void]$sigBodies.Append(", const_cast<void *>(reinterpret_cast<const void *>(&$($p.n)))")
      }
      [void]$sigBodies.Append(" };`n")
      [void]$sigBodies.Append("    QMetaObject::activate(this, &staticMetaObject, $i, _a);`n")
    }
    [void]$sigBodies.Append("}`n")

    # IndexOfMethod block
    [void]$indexBlocks.Append("        {`n")
    [void]$indexBlocks.Append("            using _t = void ($fq::*)($pmfTypes);`n")
    [void]$indexBlocks.Append("            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&$fq::$($sig.name)))`n")
    [void]$indexBlocks.Append("                { *result = $i; return; }`n")
    [void]$indexBlocks.Append("        }`n")
  }

  $indexOfBody = $indexBlocks.ToString()
  $signalBodiesCpp = $sigBodies.ToString()

  $qsm = "qt_meta_stringdata_$key"
  $qmd = "qt_meta_data_$key"

  $headerInclude = $class.cls + '.h'

  $unusedArgs = "    Q_UNUSED(_o);`n    Q_UNUSED(_id);"
  # For classes with no signals, IndexOfMethod body is empty.

  $cpp = @"
// SPDX-License-Identifier: Apache-2.0
//
// Hand-written moc output for $($fq).
//
// Generated to match Qt 6 (moc revision 12) so the project can build without
// running moc.exe (blocked in this environment by policy). The metaobject
// table contains only signals; slots are connected via the function-pointer
// overload of QObject::connect and therefore need not be registered.

#if 0
#pragma qt_no_skip_meta_object_directive
#endif

#include "$headerInclude"

#include <cstring>
#include <QtCore/qmetatype.h>

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct ${qsm}_t {
    const uint offsetsAndSizes[$($offArr.Count)];
    char stringdata0[$sdSize];
};
#define QT_STRINGIFY_COMPONENT(x) x,
static const ${qsm}_t $qsm = {
    {
$offCpp
    },
    {
$stringdata0Lit
    }
};
#undef QT_STRINGIFY_COMPONENT
static const uint $qmd[] = {
 // content:
       12,       // revision
        0,       // classname
        0,    0, // classinfo
  $methodCount,   14, // methods: count, offset
        0,    0, // properties
        0,    0, // enums/sets
        0,    0, // constructors
        0,       // flags
$signalCount,       // signalCount
 // methods: name, argc, parameters, tag, flags, metatypeoffset
$dataCpp
};
const QMetaObject $fq::staticMetaObject = {{
    QMetaObject::SuperData::link<$($class.parentMeta)>(),
    $qsm.offsetsAndSizes,
    $qmd,
    qt_static_metacall,
    nullptr,
    nullptr
}};
const QMetaObject *$fq::metaObject() const
{
    return &staticMetaObject;
}
void *$fq::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, $qsm.stringdata0))
        return static_cast<void *>(this);
    return $($class.parent)::qt_metacast(_clname);
}
int $fq::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = $($class.parent)::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < $methodCount)
            qt_static_metacall(this, _c, _id, _a);
        _id -= $methodCount;
    } else if (_c == QMetaObject::RegisterMethodArgumentData) {
        if (_id < $methodCount)
            qt_static_metacall(this, _c, _id, _a);
        _id -= $methodCount;
    }
    return _id;
}
void $fq::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
$unusedArgs
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
$indexOfBody    }
}
$signalBodiesCpp
QT_WARNING_POP
QT_END_MOC_NAMESPACE

"@

  return $cpp
}

# ---- emit files -----------------------------------------------------------
$outDir = '$PSScriptRoot\..\src'
foreach ($c in $classes) {
  $cpp = Gen-File $c
  $path = Join-Path $outDir ("moc_" + $c.cls + ".cpp")
  [System.IO.File]::WriteAllText($path, $cpp, (New-Object System.Text.UTF8Encoding $false))
  $lines = ($cpp -split "`n").Count
  Write-Output ("wrote {0,-28} {1,5} lines, {2} signals" -f $path, $lines, $c.signals.Count)
}

