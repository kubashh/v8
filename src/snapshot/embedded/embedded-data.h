// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_
#define V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_

#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

class Code;
class Isolate;

// Wraps an off-heap instruction stream.
// TODO(jgruber,v8:6666): Remove this class.
class InstructionStream final : public AllStatic {
 public:
  // Returns true, iff the given pc points into an off-heap instruction stream.
  static bool PcIsOffHeap(Isolate* isolate, Address pc);

  // If the address belongs to the embedded code blob, predictably converts it
  // to uint32 by calculating offset from the embedded code blob start and
  // returns true, and false otherwise.
  static bool TryGetAddressForHashing(Isolate* isolate, Address address,
                                      uint32_t* hashable_address);

  // Returns the corresponding builtin ID if lookup succeeds, and kNoBuiltinId
  // otherwise.
  static Builtins::Name TryLookupCode(Isolate* isolate, Address address);

  // During snapshot creation, we first create an executable off-heap area
  // containing all off-heap code. The area is guaranteed to be contiguous.
  // Note that this only applies when building the snapshot, e.g. for
  // mksnapshot. Otherwise, off-heap code is embedded directly into the binary.
  static void CreateOffHeapInstructionStream(Isolate* isolate, uint8_t** code,
                                             uint32_t* code_size,
                                             uint8_t** data,
                                             uint32_t* data_size);
  static void FreeOffHeapInstructionStream(uint8_t* code, uint32_t code_size,
                                           uint8_t* data, uint32_t data_size);
};

class EmbeddedData final {
 public:
  static EmbeddedData FromIsolate(Isolate* isolate);

  static EmbeddedData FromBlob() {
    return EmbeddedData(Isolate::CurrentEmbeddedBlobCode(),
                        Isolate::CurrentEmbeddedBlobCodeSize(),
                        Isolate::CurrentEmbeddedBlobData(),
                        Isolate::CurrentEmbeddedBlobDataSize());
  }

  static EmbeddedData FromBlob(Isolate* isolate) {
    return EmbeddedData(
        isolate->embedded_blob_code(), isolate->embedded_blob_code_size(),
        isolate->embedded_blob_data(), isolate->embedded_blob_data_size());
  }

  const uint8_t* code() const { return code_; }
  uint32_t code_size() const { return code_size_; }
  const uint8_t* data() const { return data_; }
  uint32_t data_size() const { return data_size_; }

  bool IsInCodeRange(Address pc) const {
    Address start = reinterpret_cast<Address>(code_);
    return (start <= pc) && (pc < start + code_size_);
  }

  // When short builtin calls optimization is enabled for the Isolate, there
  // will be two builtins instruction streams executed: the embedded one and
  // the one un-embedded into the per-Isolate code range. In most of the cases,
  // the per-Isolate instructions will be used but in some cases (like builtin
  // calls from Wasm) the embedded instruction stream could be used.
  // If the requested PC belongs to the embedded code blob - it'll be returned,
  // and the per-Isolate blob otherwise.
  // See http://crbug.com/v8/11527 for details.
  inline static EmbeddedData GetEmbeddedDataForPC(Isolate* isolate,
                                                  Address maybe_builtin_pc) {
    EmbeddedData d = EmbeddedData::FromBlob(isolate);
    if (isolate->is_short_builtin_calls_enabled() &&
        !d.IsInCodeRange(maybe_builtin_pc)) {
      EmbeddedData global_d = EmbeddedData::FromBlob();
      // If the pc does not belong to the embedded code blob we should be using
      // the un-embedded one.
      if (global_d.IsInCodeRange(maybe_builtin_pc)) return global_d;
    }
    return d;
  }

  void Dispose() {
    delete[] code_;
    code_ = nullptr;
    delete[] data_;
    data_ = nullptr;
  }

  Address InstructionStartOfBuiltin(int i) const;
  uint32_t InstructionSizeOfBuiltin(int i) const;

  Address InstructionStartOfBytecodeHandlers() const;
  Address InstructionEndOfBytecodeHandlers() const;

  Address MetadataStartOfBuiltin(int i) const;
  uint32_t MetadataSizeOfBuiltin(int i) const;

  uint32_t AddressForHashing(Address addr) {
    DCHECK(IsInCodeRange(addr));
    Address start = reinterpret_cast<Address>(code_);
    return static_cast<uint32_t>(addr - start);
  }

  // Padded with kCodeAlignment.
  // TODO(v8:11045): Consider removing code alignment.
  uint32_t PaddedInstructionSizeOfBuiltin(int i) const {
    uint32_t size = InstructionSizeOfBuiltin(i);
    CHECK_NE(size, 0);
    return PadAndAlignCode(size);
  }

  size_t CreateEmbeddedBlobDataHash() const;
  size_t CreateEmbeddedBlobCodeHash() const;
  size_t EmbeddedBlobDataHash() const {
    return *reinterpret_cast<const size_t*>(data_ +
                                            EmbeddedBlobDataHashOffset());
  }
  size_t EmbeddedBlobCodeHash() const {
    return *reinterpret_cast<const size_t*>(data_ +
                                            EmbeddedBlobCodeHashOffset());
  }

  size_t IsolateHash() const {
    return *reinterpret_cast<const size_t*>(data_ + IsolateHashOffset());
  }

  // Blob layout information for a single instruction stream. Corresponds
  // roughly to Code object layout (see the instruction and metadata area).
  struct LayoutDescription {
    // The offset and (unpadded) length of this builtin's instruction area
    // from the start of the embedded code section.
    uint32_t instruction_offset;
    uint32_t instruction_length;
    // The offset and (unpadded) length of this builtin's metadata area
    // from the start of the embedded code section.
    uint32_t metadata_offset;
    uint32_t metadata_length;
  };
  STATIC_ASSERT(offsetof(LayoutDescription, instruction_offset) ==
                0 * kUInt32Size);
  STATIC_ASSERT(offsetof(LayoutDescription, instruction_length) ==
                1 * kUInt32Size);
  STATIC_ASSERT(offsetof(LayoutDescription, metadata_offset) ==
                2 * kUInt32Size);
  STATIC_ASSERT(offsetof(LayoutDescription, metadata_length) ==
                3 * kUInt32Size);
  STATIC_ASSERT(sizeof(LayoutDescription) == 4 * kUInt32Size);

  static constexpr int kBuiltinCount = Builtins::builtin_count;

  // The layout of the blob is as follows:
  //
  // data:
  // [0] hash of the data section
  // [1] hash of the code section
  // [2] hash of embedded-blob-relevant heap objects
  // [3] layout description of instruction stream 0
  // ... layout descriptions
  // [x] metadata section of builtin 0
  // ... metadata sections
  //
  // code:
  // [0] instruction section of builtin 0
  // ... instruction sections

  static constexpr uint32_t kTableSize = static_cast<uint32_t>(kBuiltinCount);
  static constexpr uint32_t EmbeddedBlobDataHashOffset() { return 0; }
  static constexpr uint32_t EmbeddedBlobDataHashSize() { return kSizetSize; }
  static constexpr uint32_t EmbeddedBlobCodeHashOffset() {
    return EmbeddedBlobDataHashOffset() + EmbeddedBlobDataHashSize();
  }
  static constexpr uint32_t EmbeddedBlobCodeHashSize() { return kSizetSize; }
  static constexpr uint32_t IsolateHashOffset() {
    return EmbeddedBlobCodeHashOffset() + EmbeddedBlobCodeHashSize();
  }
  static constexpr uint32_t IsolateHashSize() { return kSizetSize; }
  static constexpr uint32_t LayoutDescriptionTableOffset() {
    return IsolateHashOffset() + IsolateHashSize();
  }
  static constexpr uint32_t LayoutDescriptionTableSize() {
    return sizeof(struct LayoutDescription) * kTableSize;
  }
  static constexpr uint32_t FixedDataSize() {
    return LayoutDescriptionTableOffset() + LayoutDescriptionTableSize();
  }
  // The variable-size data section starts here.
  static constexpr uint32_t RawMetadataOffset() { return FixedDataSize(); }

  // Code is in its own dedicated section.
  static constexpr uint32_t RawCodeOffset() { return 0; }

 private:
  EmbeddedData(const uint8_t* code, uint32_t code_size, const uint8_t* data,
               uint32_t data_size)
      : code_(code), code_size_(code_size), data_(data), data_size_(data_size) {
    DCHECK_NOT_NULL(code);
    DCHECK_LT(0, code_size);
    DCHECK_NOT_NULL(data);
    DCHECK_LT(0, data_size);
  }

  const uint8_t* RawCode() const { return code_ + RawCodeOffset(); }

  const LayoutDescription* LayoutDescription() const {
    return reinterpret_cast<const struct LayoutDescription*>(
        data_ + LayoutDescriptionTableOffset());
  }
  const uint8_t* RawMetadata() const { return data_ + RawMetadataOffset(); }

  static constexpr int PadAndAlignCode(int size) {
    // Ensure we have at least one byte trailing the actual builtin
    // instructions which we can later fill with int3.
    return RoundUp<kCodeAlignment>(size + 1);
  }
  static constexpr int PadAndAlignData(int size) {
    // Ensure we have at least one byte trailing the actual builtin
    // instructions which we can later fill with int3.
    return RoundUp<Code::kMetadataAlignment>(size);
  }

  void PrintStatistics() const;

  // The code section contains instruction streams. It is guaranteed to have
  // execute permissions, and may have read permissions.
  const uint8_t* code_;
  uint32_t code_size_;

  // The data section contains both descriptions of the code section (hashes,
  // offsets, sizes) and metadata describing Code objects (see
  // Code::MetadataStart()). It is guaranteed to have read permissions.
  const uint8_t* data_;
  uint32_t data_size_;
};

constexpr int kIndexMap[] = {
    82,   83,   84,   139,  140,  141,  142,  163,  164,  173,  175,  177,
    178,  190,  191,  192,  201,  202,  203,  204,  205,  206,  207,  208,
    209,  210,  211,  212,  213,  214,  215,  216,  217,  218,  219,  220,
    221,  222,  223,  224,  225,  226,  227,  228,  229,  230,  231,  232,
    233,  234,  235,  236,  237,  238,  239,  240,  241,  242,  243,  244,
    245,  246,  247,  248,  249,  250,  271,  272,  273,  274,  275,  276,
    277,  278,  279,  280,  281,  282,  283,  284,  285,  286,  287,  288,
    289,  290,  291,  292,  293,  294,  295,  296,  297,  298,  299,  300,
    302,  304,  307,  311,  313,  314,  315,  316,  317,  318,  319,  322,
    323,  362,  370,  371,  372,  373,  400,  401,  402,  403,  405,  407,
    409,  411,  412,  414,  415,  418,  419,  420,  421,  431,  432,  433,
    434,  435,  436,  437,  438,  439,  440,  441,  442,  443,  445,  446,
    447,  448,  449,  451,  452,  461,  468,  478,  479,  480,  481,  482,
    486,  489,  493,  494,  495,  496,  499,  503,  504,  505,  506,  507,
    508,  537,  574,  575,  576,  578,  579,  1055, 1056, 1057, 1058, 1059,
    1060, 1061, 1062, 1063, 1064, 1065, 1066, 1067, 1068, 1069, 1070, 1071,
    1072, 1073, 1074, 1075, 1076, 1079, 1080, 1081, 1082, 1083, 1084, 1085,
    1086, 1087, 1088, 1089, 1090, 1091, 1092, 1093, 1094, 1095, 1096, 1097,
    1098, 1099, 1100, 1101, 1102, 1103, 1104, 1105, 1106, 1107, 1108, 1109,
    1110, 1111, 1112, 1113, 1114, 1115, 1116, 1117, 1118, 1119, 1121, 1123,
    1124, 1125, 1126, 1127, 1128, 1129, 1130, 1131, 1132, 1133, 1134, 1135,
    2,    554,  803,  422,  688,  568,  1175, 1228, 1224, 1238, 1234, 1191,
    777,  328,  325,  1163, 1174, 1173, 1289, 1149, 1307, 1147, 1179, 1161,
    1182, 1162, 1288, 1153, 1249, 1152, 1165, 1136, 1150, 1184, 1176, 1154,
    1276, 1148, 1226, 1251, 1230, 1241, 1291, 1290, 1218, 1250, 1217, 1185,
    1303, 1252, 50,   1227, 1295, 1225, 1292, 1231, 1213, 1275, 1301, 1336,
    1266, 40,   1160, 1283, 1302, 1294, 1284, 1296, 1255, 1293, 1137, 1256,
    1219, 1186, 340,  1189, 564,  1242, 330,  1151, 1304, 1334, 1246, 1263,
    1287, 1248, 1277, 1164, 1214, 1299, 1300, 1435, 1280, 1209, 1261, 1243,
    1478, 1308, 1286, 1201, 1220, 775,  1202, 1262, 1282, 1229, 1297, 682,
    1259, 104,  1326, 667,  1247, 1245, 1155, 1240, 1382, 1197, 1166, 1298,
    1534, 347,  348,  428,  1258, 668,  689,  692,  1244, 134,  1335, 1190,
    1347, 1333, 1357, 1327, 1352, 1172, 1338, 127,  1159, 1254, 1221, 1355,
    1272, 1532, 1429, 1257, 1271, 55,   1396, 1359, 1157, 52,   1397, 1400,
    1349, 1195, 1375, 1398, 1423, 1394, 1528, 1376, 1424, 1358, 1346, 1401,
    1426, 1399, 1462, 1156, 571,  1363, 1451, 1215, 1364, 1386, 1380, 1348,
    1449, 1448, 1526, 1408, 1268, 1362, 1425, 1436, 1395, 1192, 838,  1203,
    1207, 1418, 1412, 1328, 1460, 1198, 1196, 1331, 1432, 1411, 1216, 1305,
    1450, 1267, 1208, 1329, 1413, 1235, 1330, 1223, 612,  658,  691,  693,
    897,  522,  521,  333,  331,  24,   669,  1269, 1158, 1273, 339,  338,
    105,  327,  324,  144,  145,  1120, 1122, 490,  53,   56,   861,  453,
    398,  136,  337,  336,  112,  636,  646,  681,  179,  133,  360,  356,
    839,  569,  174,  641,  181,  487,  859,  565,  774,  773,  994,  54,
    524,  531,  406,  687,  589,  588,  647,  635,  1315, 729,  730,  642,
    176,  570,  1002, 1001, 998,  997,  1000, 999,  132,  830,  834,  806,
    492,  866,  454,  832,  833,  835,  130,  722,  603,  602,  676,  523,
    690,  520,  358,  361,  665,  389,  856,  868,  376,  457,  666,  823,
    614,  426,  391,  321,  332,  357,  110,  359,  604,  0,    1,    3,
    4,    5,    6,    7,    8,    9,    10,   11,   12,   13,   14,   15,
    16,   17,   18,   19,   20,   21,   22,   23,   25,   26,   27,   28,
    29,   30,   31,   32,   33,   34,   35,   36,   37,   38,   39,   41,
    42,   43,   44,   45,   46,   47,   48,   49,   51,   57,   58,   59,
    60,   61,   62,   63,   64,   65,   66,   67,   68,   69,   70,   71,
    72,   73,   74,   75,   76,   77,   78,   79,   80,   81,   85,   86,
    87,   88,   89,   90,   91,   92,   93,   94,   95,   96,   97,   98,
    99,   100,  101,  102,  103,  106,  107,  108,  109,  111,  113,  114,
    115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,  126,
    128,  129,  131,  135,  137,  138,  143,  146,  147,  148,  149,  150,
    151,  152,  153,  154,  155,  156,  157,  158,  159,  160,  161,  162,
    165,  166,  167,  168,  169,  170,  171,  172,  180,  182,  183,  184,
    185,  186,  187,  188,  189,  193,  194,  195,  196,  197,  198,  199,
    200,  251,  252,  253,  254,  255,  256,  257,  258,  259,  260,  261,
    262,  263,  264,  265,  266,  267,  268,  269,  270,  301,  303,  305,
    306,  308,  309,  310,  312,  320,  326,  329,  334,  335,  341,  342,
    343,  344,  345,  346,  349,  350,  351,  352,  353,  354,  355,  363,
    364,  365,  366,  367,  368,  369,  374,  375,  377,  378,  379,  380,
    381,  382,  383,  384,  385,  386,  387,  388,  390,  392,  393,  394,
    395,  396,  397,  399,  404,  408,  410,  413,  416,  417,  423,  424,
    425,  427,  429,  430,  444,  450,  455,  456,  458,  459,  460,  462,
    463,  464,  465,  466,  467,  469,  470,  471,  472,  473,  474,  475,
    476,  477,  483,  484,  485,  488,  491,  497,  498,  500,  501,  502,
    509,  510,  511,  512,  513,  514,  515,  516,  517,  518,  519,  525,
    526,  527,  528,  529,  530,  532,  533,  534,  535,  536,  538,  539,
    540,  541,  542,  543,  544,  545,  546,  547,  548,  549,  550,  551,
    552,  553,  555,  556,  557,  558,  559,  560,  561,  562,  563,  566,
    567,  572,  573,  577,  580,  581,  582,  583,  584,  585,  586,  587,
    590,  591,  592,  593,  594,  595,  596,  597,  598,  599,  600,  601,
    605,  606,  607,  608,  609,  610,  611,  613,  615,  616,  617,  618,
    619,  620,  621,  622,  623,  624,  625,  626,  627,  628,  629,  630,
    631,  632,  633,  634,  637,  638,  639,  640,  643,  644,  645,  648,
    649,  650,  651,  652,  653,  654,  655,  656,  657,  659,  660,  661,
    662,  663,  664,  670,  671,  672,  673,  674,  675,  677,  678,  679,
    680,  683,  684,  685,  686,  694,  695,  696,  697,  698,  699,  700,
    701,  702,  703,  704,  705,  706,  707,  708,  709,  710,  711,  712,
    713,  714,  715,  716,  717,  718,  719,  720,  721,  723,  724,  725,
    726,  727,  728,  731,  732,  733,  734,  735,  736,  737,  738,  739,
    740,  741,  742,  743,  744,  745,  746,  747,  748,  749,  750,  751,
    752,  753,  754,  755,  756,  757,  758,  759,  760,  761,  762,  763,
    764,  765,  766,  767,  768,  769,  770,  771,  772,  776,  778,  779,
    780,  781,  782,  783,  784,  785,  786,  787,  788,  789,  790,  791,
    792,  793,  794,  795,  796,  797,  798,  799,  800,  801,  802,  804,
    805,  807,  808,  809,  810,  811,  812,  813,  814,  815,  816,  817,
    818,  819,  820,  821,  822,  824,  825,  826,  827,  828,  829,  831,
    836,  837,  840,  841,  842,  843,  844,  845,  846,  847,  848,  849,
    850,  851,  852,  853,  854,  855,  857,  858,  860,  862,  863,  864,
    865,  867,  869,  870,  871,  872,  873,  874,  875,  876,  877,  878,
    879,  880,  881,  882,  883,  884,  885,  886,  887,  888,  889,  890,
    891,  892,  893,  894,  895,  896,  898,  899,  900,  901,  902,  903,
    904,  905,  906,  907,  908,  909,  910,  911,  912,  913,  914,  915,
    916,  917,  918,  919,  920,  921,  922,  923,  924,  925,  926,  927,
    928,  929,  930,  931,  932,  933,  934,  935,  936,  937,  938,  939,
    940,  941,  942,  943,  944,  945,  946,  947,  948,  949,  950,  951,
    952,  953,  954,  955,  956,  957,  958,  959,  960,  961,  962,  963,
    964,  965,  966,  967,  968,  969,  970,  971,  972,  973,  974,  975,
    976,  977,  978,  979,  980,  981,  982,  983,  984,  985,  986,  987,
    988,  989,  990,  991,  992,  993,  995,  996,  1003, 1004, 1005, 1006,
    1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 1015, 1016, 1017, 1018,
    1019, 1020, 1021, 1022, 1023, 1024, 1025, 1026, 1027, 1028, 1029, 1030,
    1031, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041, 1042,
    1043, 1044, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052, 1053, 1054,
    1077, 1078, 1138, 1139, 1140, 1141, 1142, 1143, 1144, 1145, 1146, 1167,
    1168, 1169, 1170, 1171, 1177, 1178, 1180, 1181, 1183, 1187, 1188, 1193,
    1194, 1199, 1200, 1204, 1205, 1206, 1210, 1211, 1212, 1222, 1232, 1233,
    1236, 1237, 1239, 1253, 1260, 1264, 1265, 1270, 1274, 1278, 1279, 1281,
    1285, 1306, 1309, 1310, 1311, 1312, 1313, 1314, 1316, 1317, 1318, 1319,
    1320, 1321, 1322, 1323, 1324, 1325, 1332, 1337, 1339, 1340, 1341, 1342,
    1343, 1344, 1345, 1350, 1351, 1353, 1354, 1356, 1360, 1361, 1365, 1366,
    1367, 1368, 1369, 1370, 1371, 1372, 1373, 1374, 1377, 1378, 1379, 1381,
    1383, 1384, 1385, 1387, 1388, 1389, 1390, 1391, 1392, 1393, 1402, 1403,
    1404, 1405, 1406, 1407, 1409, 1410, 1414, 1415, 1416, 1417, 1419, 1420,
    1421, 1422, 1427, 1428, 1430, 1431, 1433, 1434, 1437, 1438, 1439, 1440,
    1441, 1442, 1443, 1444, 1445, 1446, 1447, 1452, 1453, 1454, 1455, 1456,
    1457, 1458, 1459, 1461, 1463, 1464, 1465, 1466, 1467, 1468, 1469, 1470,
    1471, 1472, 1473, 1474, 1475, 1476, 1477, 1479, 1480, 1481, 1482, 1483,
    1484, 1485, 1486, 1487, 1488, 1489, 1490, 1491, 1492, 1493, 1494, 1495,
    1496, 1497, 1498, 1499, 1500, 1501, 1502, 1503, 1504, 1505, 1506, 1507,
    1508, 1509, 1510, 1511, 1512, 1513, 1514, 1515, 1516, 1517, 1518, 1519,
    1520, 1521, 1522, 1523, 1524, 1525, 1527, 1529, 1530, 1531, 1533, 1535,
    1536, 1537, 1538, 1539, 1540, 1541, 1542, 1543, 1544, 1545, 1546, 1547,
    1548, 1549, 1550, 1551, 1552, 1553, 1554, 1555, 1556, 1557, 1558, 1559,
    1560, 1561, 1562, 1563, 1564, 1565, 1566, 1567, 1568, 1569, 1570, 1571,
    1572, 1573, 1574, 1575, 1576, 1577, 1578, 1579, 1580, 1581, 1582, 1583,
    1584, 1585, 1586, 1587, 1588, 1589, 1590, 1591, 1592, 1593, 1594, 1595,
    1596, 1597, 1598, 1599, 1600, 1601, 1602, 1603, 1604, 1605, 1606, 1607,
    1608, 1609, 1610, 1611, 1612, 1613, 1614, 1615, 1616, 1617, 1618, 1619,
    1620, 1621, 1622, 1623};

constexpr int kBuiltinMap[] = {
    573,  574,  252,  575,  576,  577,  578,  579,  580,  581,  582,  583,
    584,  585,  586,  587,  588,  589,  590,  591,  592,  593,  594,  595,
    473,  596,  597,  598,  599,  600,  601,  602,  603,  604,  605,  606,
    607,  608,  609,  610,  313,  611,  612,  613,  614,  615,  616,  617,
    618,  619,  302,  620,  405,  488,  515,  401,  489,  621,  622,  623,
    624,  625,  626,  627,  628,  629,  630,  631,  632,  633,  634,  635,
    636,  637,  638,  639,  640,  641,  642,  643,  644,  645,  0,    1,
    2,    646,  647,  648,  649,  650,  651,  652,  653,  654,  655,  656,
    657,  658,  659,  660,  661,  662,  663,  664,  361,  480,  665,  666,
    667,  668,  570,  669,  496,  670,  671,  672,  673,  674,  675,  676,
    677,  678,  679,  680,  681,  682,  683,  391,  684,  685,  546,  686,
    536,  501,  381,  687,  493,  688,  689,  3,    4,    5,    6,    690,
    483,  484,  691,  692,  693,  694,  695,  696,  697,  698,  699,  700,
    701,  702,  703,  704,  705,  706,  707,  7,    8,    708,  709,  710,
    711,  712,  713,  714,  715,  9,    506,  10,   528,  11,   12,   500,
    716,  508,  717,  718,  719,  720,  721,  722,  723,  724,  13,   14,
    15,   725,  726,  727,  728,  729,  730,  731,  732,  16,   17,   18,
    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,
    31,   32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,
    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,   53,   54,
    55,   56,   57,   58,   59,   60,   61,   62,   63,   64,   65,   733,
    734,  735,  736,  737,  738,  739,  740,  741,  742,  743,  744,  745,
    746,  747,  748,  749,  750,  751,  752,  66,   67,   68,   69,   70,
    71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,   82,
    83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,
    95,   753,  96,   754,  97,   755,  756,  98,   757,  758,  759,  99,
    760,  100,  101,  102,  103,  104,  105,  106,  761,  567,  107,  108,
    482,  266,  762,  481,  265,  763,  330,  472,  568,  471,  764,  765,
    495,  494,  479,  478,  326,  766,  767,  768,  769,  770,  771,  373,
    374,  772,  773,  774,  775,  776,  777,  778,  503,  569,  554,  571,
    502,  555,  109,  779,  780,  781,  782,  783,  784,  785,  110,  111,
    112,  113,  786,  787,  560,  788,  789,  790,  791,  792,  793,  794,
    795,  796,  797,  798,  799,  557,  800,  566,  801,  802,  803,  804,
    805,  806,  492,  807,  114,  115,  116,  117,  808,  118,  518,  119,
    809,  120,  810,  121,  122,  811,  123,  124,  812,  813,  125,  126,
    127,  128,  255,  814,  815,  816,  565,  817,  375,  818,  819,  129,
    130,  131,  132,  133,  134,  135,  136,  137,  138,  139,  140,  141,
    820,  142,  143,  144,  145,  146,  821,  147,  148,  491,  542,  822,
    823,  561,  824,  825,  826,  149,  827,  828,  829,  830,  831,  832,
    150,  833,  834,  835,  836,  837,  838,  839,  840,  841,  151,  152,
    153,  154,  155,  842,  843,  844,  156,  509,  845,  157,  487,  846,
    540,  158,  159,  160,  161,  847,  848,  162,  849,  850,  851,  163,
    164,  165,  166,  167,  168,  852,  853,  854,  855,  856,  857,  858,
    859,  860,  861,  862,  553,  470,  469,  551,  516,  863,  864,  865,
    866,  867,  868,  517,  869,  870,  871,  872,  873,  169,  874,  875,
    876,  877,  878,  879,  880,  881,  882,  883,  884,  885,  886,  887,
    888,  889,  253,  890,  891,  892,  893,  894,  895,  896,  897,  898,
    328,  511,  899,  900,  257,  505,  529,  424,  901,  902,  170,  171,
    172,  903,  173,  174,  904,  905,  906,  907,  908,  909,  910,  911,
    521,  520,  912,  913,  914,  915,  916,  917,  918,  919,  920,  921,
    922,  923,  549,  548,  572,  924,  925,  926,  927,  928,  929,  930,
    464,  931,  564,  932,  933,  934,  935,  936,  937,  938,  939,  940,
    941,  942,  943,  944,  945,  946,  947,  948,  949,  950,  951,  523,
    497,  952,  953,  954,  955,  507,  527,  956,  957,  958,  498,  522,
    959,  960,  961,  962,  963,  964,  965,  966,  967,  968,  465,  969,
    970,  971,  972,  973,  974,  556,  562,  363,  377,  474,  975,  976,
    977,  978,  979,  980,  550,  981,  982,  983,  984,  499,  359,  985,
    986,  987,  988,  519,  256,  378,  552,  466,  379,  467,  989,  990,
    991,  992,  993,  994,  995,  996,  997,  998,  999,  1000, 1001, 1002,
    1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014,
    1015, 1016, 547,  1017, 1018, 1019, 1020, 1021, 1022, 525,  526,  1023,
    1024, 1025, 1026, 1027, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035,
    1036, 1037, 1038, 1039, 1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047,
    1048, 1049, 1050, 1051, 1052, 1053, 1054, 1055, 1056, 1057, 1058, 1059,
    1060, 1061, 1062, 1063, 1064, 513,  512,  353,  1065, 264,  1066, 1067,
    1068, 1069, 1070, 1071, 1072, 1073, 1074, 1075, 1076, 1077, 1078, 1079,
    1080, 1081, 1082, 1083, 1084, 1085, 1086, 1087, 1088, 1089, 1090, 254,
    1091, 1092, 539,  1093, 1094, 1095, 1096, 1097, 1098, 1099, 1100, 1101,
    1102, 1103, 1104, 1105, 1106, 1107, 1108, 563,  1109, 1110, 1111, 1112,
    1113, 1114, 537,  1115, 543,  544,  538,  545,  1116, 1117, 442,  504,
    1118, 1119, 1120, 1121, 1122, 1123, 1124, 1125, 1126, 1127, 1128, 1129,
    1130, 1131, 1132, 1133, 558,  1134, 1135, 510,  1136, 490,  1137, 1138,
    1139, 1140, 541,  1141, 559,  1142, 1143, 1144, 1145, 1146, 1147, 1148,
    1149, 1150, 1151, 1152, 1153, 1154, 1155, 1156, 1157, 1158, 1159, 1160,
    1161, 1162, 1163, 1164, 1165, 1166, 1167, 1168, 1169, 468,  1170, 1171,
    1172, 1173, 1174, 1175, 1176, 1177, 1178, 1179, 1180, 1181, 1182, 1183,
    1184, 1185, 1186, 1187, 1188, 1189, 1190, 1191, 1192, 1193, 1194, 1195,
    1196, 1197, 1198, 1199, 1200, 1201, 1202, 1203, 1204, 1205, 1206, 1207,
    1208, 1209, 1210, 1211, 1212, 1213, 1214, 1215, 1216, 1217, 1218, 1219,
    1220, 1221, 1222, 1223, 1224, 1225, 1226, 1227, 1228, 1229, 1230, 1231,
    1232, 1233, 1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241, 1242, 1243,
    1244, 1245, 1246, 1247, 1248, 1249, 1250, 1251, 1252, 1253, 1254, 1255,
    1256, 1257, 1258, 1259, 1260, 1261, 1262, 1263, 1264, 1265, 514,  1266,
    1267, 533,  532,  535,  534,  531,  530,  1268, 1269, 1270, 1271, 1272,
    1273, 1274, 1275, 1276, 1277, 1278, 1279, 1280, 1281, 1282, 1283, 1284,
    1285, 1286, 1287, 1288, 1289, 1290, 1291, 1292, 1293, 1294, 1295, 1296,
    1297, 1298, 1299, 1300, 1301, 1302, 1303, 1304, 1305, 1306, 1307, 1308,
    1309, 1310, 1311, 1312, 1313, 1314, 1315, 1316, 1317, 1318, 1319, 175,
    176,  177,  178,  179,  180,  181,  182,  183,  184,  185,  186,  187,
    188,  189,  190,  191,  192,  193,  194,  195,  196,  1320, 1321, 197,
    198,  199,  200,  201,  202,  203,  204,  205,  206,  207,  208,  209,
    210,  211,  212,  213,  214,  215,  216,  217,  218,  219,  220,  221,
    222,  223,  224,  225,  226,  227,  228,  229,  230,  231,  232,  233,
    234,  235,  236,  237,  485,  238,  486,  239,  240,  241,  242,  243,
    244,  245,  246,  247,  248,  249,  250,  251,  283,  322,  1322, 1323,
    1324, 1325, 1326, 1327, 1328, 1329, 1330, 273,  289,  271,  284,  331,
    281,  279,  287,  366,  423,  404,  476,  392,  314,  275,  277,  267,
    339,  282,  370,  1331, 1332, 1333, 1334, 1335, 389,  269,  268,  258,
    286,  1336, 1337, 274,  1338, 1339, 276,  1340, 285,  299,  325,  1341,
    1342, 327,  383,  263,  441,  1343, 1344, 409,  450,  369,  449,  1345,
    1346, 351,  354,  443,  1347, 1348, 1349, 444,  458,  345,  1350, 1351,
    1352, 308,  340,  427,  454,  298,  296,  324,  352,  394,  1353, 463,
    260,  305,  290,  303,  259,  357,  292,  307,  1354, 1355, 262,  461,
    1356, 1357, 261,  1358, 367,  293,  329,  347,  380,  365,  334,  364,
    337,  280,  297,  291,  301,  1359, 393,  320,  323,  399,  376,  360,
    1360, 346,  355,  335,  1361, 1362, 312,  457,  436,  475,  1363, 400,
    396,  477,  1364, 309,  288,  338,  1365, 1366, 344,  1367, 356,  315,
    318,  1368, 350,  336,  278,  270,  295,  294,  306,  321,  317,  304,
    319,  358,  371,  341,  342,  310,  316,  300,  332,  455,  1369, 272,
    349,  1370, 1371, 1372, 1373, 1374, 1375, 524,  1376, 1377, 1378, 1379,
    1380, 1381, 1382, 1383, 1384, 1385, 362,  387,  447,  459,  462,  451,
    1386, 385,  333,  382,  311,  1387, 390,  1388, 1389, 1390, 1391, 1392,
    1393, 1394, 418,  384,  431,  408,  1395, 1396, 388,  1397, 1398, 395,
    1399, 386,  417,  403,  1400, 1401, 437,  425,  428,  1402, 1403, 1404,
    1405, 1406, 1407, 1408, 1409, 1410, 1411, 410,  415,  1412, 1413, 1414,
    430,  1415, 368,  1416, 1417, 1418, 429,  1419, 1420, 1421, 1422, 1423,
    1424, 1425, 413,  440,  402,  406,  411,  421,  407,  419,  1426, 1427,
    1428, 1429, 1430, 1431, 435,  1432, 1433, 453,  446,  460,  1434, 1435,
    1436, 1437, 445,  1438, 1439, 1440, 1441, 412,  416,  438,  420,  1442,
    1443, 398,  1444, 1445, 452,  1446, 1447, 343,  439,  1448, 1449, 1450,
    1451, 1452, 1453, 1454, 1455, 1456, 1457, 1458, 433,  432,  456,  426,
    1459, 1460, 1461, 1462, 1463, 1464, 1465, 1466, 448,  1467, 422,  1468,
    1469, 1470, 1471, 1472, 1473, 1474, 1475, 1476, 1477, 1478, 1479, 1480,
    1481, 1482, 348,  1483, 1484, 1485, 1486, 1487, 1488, 1489, 1490, 1491,
    1492, 1493, 1494, 1495, 1496, 1497, 1498, 1499, 1500, 1501, 1502, 1503,
    1504, 1505, 1506, 1507, 1508, 1509, 1510, 1511, 1512, 1513, 1514, 1515,
    1516, 1517, 1518, 1519, 1520, 1521, 1522, 1523, 1524, 1525, 1526, 1527,
    1528, 1529, 434,  1530, 414,  1531, 1532, 1533, 397,  1534, 372,  1535,
    1536, 1537, 1538, 1539, 1540, 1541, 1542, 1543, 1544, 1545, 1546, 1547,
    1548, 1549, 1550, 1551, 1552, 1553, 1554, 1555, 1556, 1557, 1558, 1559,
    1560, 1561, 1562, 1563, 1564, 1565, 1566, 1567, 1568, 1569, 1570, 1571,
    1572, 1573, 1574, 1575, 1576, 1577, 1578, 1579, 1580, 1581, 1582, 1583,
    1584, 1585, 1586, 1587, 1588, 1589, 1590, 1591, 1592, 1593, 1594, 1595,
    1596, 1597, 1598, 1599, 1600, 1601, 1602, 1603, 1604, 1605, 1606, 1607,
    1608, 1609, 1610, 1611, 1612, 1613, 1614, 1615, 1616, 1617, 1618, 1619,
    1620, 1621, 1622, 1623};

constexpr int MapEmbeddedIndexToBuiltinIndex(int embedded_index) {
  return kIndexMap[embedded_index];
}

constexpr int MapBuiltinIndexToEmbeddedIndex(int builtin_index) {
  return kBuiltinMap[builtin_index];
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_
