/*
 * Exact immutable manifest for the 113 HP_*.wis assets in
 * Azrok_D3D9Web_pathouse_guestprofiles6_rsdefaults1_utf8ime2_
 * dynamicaccounts2_startersplash1_fastboot4.zip.
 *
 * Generated data is deliberately compile-time-only.  The HLE compares the
 * live FsZip entry identity and the content SHA-256 reconstructed from the
 * original loader descriptor before an entry may become a hit.
 */

#ifndef __WASM_JIT_AZROK_FACE_WIS_MANIFEST_H__
#define __WASM_JIT_AZROK_FACE_WIS_MANIFEST_H__

#if defined(BOXEDWINE_WASM_JIT_AZROK_FACE_WIS_HLE)

struct WasmJitAzrokFaceWisManifestEntry {
    const char* canonicalPath;
    U32 crc32;
    U64 centralDirectoryOffset;
    U64 compressedLength;
    const char* sha256;
};

struct WasmJitAzrokFaceWisHashIndex {
    U32 fnv1a;
    U16 manifestIndex;
};

static constexpr U32 WASM_JIT_AZROK_FACE_WIS_FILE_BYTES = 130580u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_HEADER_BYTES = 20u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_PAYLOAD_BYTES = 0x1fe00u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_COPY_BYTES = 0x20000u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_TAIL_BYTES = 0x200u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_WIDTH = 256u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_HEIGHT = 255u;
static constexpr U32 WASM_JIT_AZROK_FACE_WIS_COMPRESSION = 8u;

static constexpr U8 WASM_JIT_AZROK_FACE_WIS_HEADER[20] = {
    0x02,0x00,0x00,0x00, 0x00,0x01,0x00,0x00,
    0xff,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0xfe,0x01,0x00,
};

static constexpr WasmJitAzrokFaceWisManifestEntry
WASM_JIT_AZROK_FACE_WIS_MANIFEST[] = {
    {"data/texture/hp_ak_b_base_0.wis", 0xc6b4146au, 156880725ULL, 5320ULL, "0fdf9eccc248de499b2a800e29bdbb25b76e36cbf2aa69cca41865bf2f11ec42"},
    {"data/texture/hp_ak_b_base_1.wis", 0x3e49ad29u, 156880927ULL, 4519ULL, "0f79eafd6b17aa6e73a71773a01a145ae2f2b63650b4238f3045773034f8c43e"},
    {"data/texture/hp_ak_b_base_2.wis", 0x31fd9de1u, 156881129ULL, 3526ULL, "fc9fdb28fde762ff865d6318c7a1b1c76db2f37f12b340d8cf22b0dcd4fe7625"},
    {"data/texture/hp_ak_b_joy_0.wis", 0x551713e1u, 156881330ULL, 5242ULL, "83cb4400f0026a8ba92ac0509b617a7d2dcba8dbac738f9b40c0b6c0f69e5358"},
    {"data/texture/hp_ak_b_sad_0.wis", 0x4cb1d463u, 156881530ULL, 5511ULL, "218fd9363cd9dfff1f409381b5e10629951ca5b484fab432948125a29dcd3711"},
    {"data/texture/hp_ak_g_base_0.wis", 0x0206fcd2u, 156881731ULL, 12169ULL, "fbddf9c71d860cac60a4c5cdb7627a5eb7595a641e3539abb9ec72bb54e9103d"},
    {"data/texture/hp_ak_g_base_1.wis", 0x1b782e0du, 156881933ULL, 10875ULL, "4762e971349177b3409971384afa9375b8e682cc9860116245e9971ce99f3b7e"},
    {"data/texture/hp_ak_g_base_2.wis", 0x1982475eu, 156882135ULL, 7370ULL, "511b00e4c204583fe173dc32d937ab9f4cd458af304a370e162f585d126c6a27"},
    {"data/texture/hp_ak_g_joy_0.wis", 0x5f7d65a4u, 156882336ULL, 9721ULL, "0e7cf67f8d2708606067339fead8a91f3b63fa0a2c19b93e9f18df9dccc02d97"},
    {"data/texture/hp_ak_g_sad_0.wis", 0x2e3dae7du, 156882536ULL, 11029ULL, "582f49fbbf6b9dca308057165a2c0d9f4b06b89c737ee418cb73c26f677334a3"},
    {"data/texture/hp_ak_g_sick_0.wis", 0x2d8bfdb0u, 156882737ULL, 13172ULL, "2a7fa367e28de77357d2fd2684686e11cac82bacd898e11f6a1baab4ad25099f"},
    {"data/texture/hp_ak_g_sick_1.wis", 0x70cf18dcu, 156882939ULL, 13218ULL, "2b4a190a8531a6091388827c5bc36fd9f5cbdf4bd937d0758fbc6eaaf2eec5c8"},
    {"data/texture/hp_ak_g_sick_2.wis", 0x8d53960fu, 156883141ULL, 11889ULL, "099bb18b99a03480c41297041d9d3ff85d228924f817d09aac5837bde5ebcf94"},
    {"data/texture/hp_ak_r_base_0.wis", 0x7e1a5104u, 156883343ULL, 3311ULL, "52f042503eaf2c8649350f4c4336a4c22f79c415e0daab12ca22e2895df3afb9"},
    {"data/texture/hp_ak_r_base_1.wis", 0x2b1906cfu, 156883545ULL, 2440ULL, "569b5125fc6a405c6fd40584473c1f5e6c6c385b34a933df4b8fc4d1a0d8642f"},
    {"data/texture/hp_ak_r_joy_0.wis", 0x8ff24428u, 156883746ULL, 3303ULL, "f081ddc69cbec4bd48478da560a1bc9c158d55e726d3297cf4f9cff54ca2ba62"},
    {"data/texture/hp_ak_r_sad_0.wis", 0xc6866759u, 156883946ULL, 3478ULL, "37c3f2cbc899bb0953f02692567542c16b5226d6ed06dd9f90f08075a93bec5d"},
    {"data/texture/hp_ak_r_sick_0.wis", 0xf96c5d28u, 156884147ULL, 5513ULL, "53c710d477f94a225b9fe7dfb4260cc53af79697418974838020d30af4a12d0b"},
    {"data/texture/hp_ak_r_sick_1.wis", 0x068eb204u, 156884349ULL, 4687ULL, "cf023d530af884828dc9f00090d287f5e67f78573759b3499010c08661e3550f"},
    {"data/texture/hp_ak_r_sick_2.wis", 0xa43303e1u, 156884551ULL, 5436ULL, "ecf572f52d132d8d2567bf4d84fc59bcfb37955b30130dfa2cf9720497e89f19"},
    {"data/texture/hp_ak_s_base_0.wis", 0xbdc65b57u, 156884753ULL, 6412ULL, "e9f71463921871ea9aff043d610c0508efa50441b862b4370034354389ada97b"},
    {"data/texture/hp_ak_s_base_1.wis", 0x75dc5520u, 156884955ULL, 5997ULL, "07c1e8c812ee960eaa94457f4c96c46f1064f9009c1b8f2cc4bc0837bee18936"},
    {"data/texture/hp_ak_s_base_2.wis", 0x5b3ace93u, 156885157ULL, 4335ULL, "5ea609861035f58a19e6669b95808e0ab1ee8d57b782e06ddd62dec97be060b5"},
    {"data/texture/hp_ak_s_joy_0.wis", 0x3c2ac473u, 156885358ULL, 3982ULL, "d088acae69647eddfd9818228cff8446c22cf9c1367fffea037aaade2f39e207"},
    {"data/texture/hp_ak_s_sad_0.wis", 0xdaafc251u, 156885558ULL, 5027ULL, "10eb6888bc4618d17f269d03186ea47af81021330080dde11c923421b4be32d8"},
    {"data/texture/hp_ak_s_sick_0.wis", 0xfff2f97cu, 156885759ULL, 8611ULL, "d011509360af20660a1d455fabbb58706ccdf0af14a8a653af73daa03a5d6f09"},
    {"data/texture/hp_ak_s_sick_1.wis", 0x99aa08d0u, 156885961ULL, 8672ULL, "98c7fed91d2023ce5c2621ca370cb121656a85e2c415c0a0164ef151aec8df13"},
    {"data/texture/hp_ak_s_sick_2.wis", 0x053b9c45u, 156886163ULL, 7640ULL, "1cb3ff1f05d561aa9b71a0cbeb64bdf2c6acd66103ac76c1e83fd917c5cba067"},
    {"data/texture/hp_ao_b_base_0.wis", 0xb3f177d3u, 156886466ULL, 3271ULL, "746d1cab46e0688246dc506369311363750ecf511cfa1e47103d8b2ba6616b97"},
    {"data/texture/hp_ao_b_base_1.wis", 0x7405b209u, 156886769ULL, 3659ULL, "38c10ade66c8f1de52b64ed08825f411f384e212786eb66289b015bde6f377a1"},
    {"data/texture/hp_ao_b_base_2.wis", 0xd41d3aa8u, 156887072ULL, 2888ULL, "1e16d344f62e2b2e8e9770a05984215a88b3b4a72a77240fb52cf7b46ee86b6d"},
    {"data/texture/hp_ao_b_joy_0.wis", 0xb3f177d3u, 156887373ULL, 3271ULL, "746d1cab46e0688246dc506369311363750ecf511cfa1e47103d8b2ba6616b97"},
    {"data/texture/hp_ao_b_joy_1.wis", 0xfb30834fu, 156887673ULL, 4063ULL, "9a649f17e997fc15eb620e2b08fce4f3344ddf047fa0b374d2c4d6ba9f01dfd1"},
    {"data/texture/hp_ao_b_sad_0.wis", 0xb3f177d3u, 156887973ULL, 3271ULL, "746d1cab46e0688246dc506369311363750ecf511cfa1e47103d8b2ba6616b97"},
    {"data/texture/hp_ao_b_sad_1.wis", 0x37216abbu, 156888273ULL, 3908ULL, "9a505cc5ba6722d558faa5537340d6cb97dcd3bafcb8fbfd4109dff45660a8bc"},
    {"data/texture/hp_ao_g_base_0.wis", 0x2fcf9d93u, 156888575ULL, 6963ULL, "47ee76a9d6cf3d34e0271f0be8acf442d841ae163f14265e97786fd9e840999f"},
    {"data/texture/hp_ao_g_base_1.wis", 0xd07268a4u, 156888878ULL, 7078ULL, "569a767ed4530f8ebed373b2e16db7f821b2a7da8fb0b7bc6bb7491764c99dcc"},
    {"data/texture/hp_ao_g_base_2.wis", 0xb2183a2au, 156889181ULL, 7193ULL, "8461f7bb6af1263c98cdeb67cb57a3277a1de841872fec24e1ab64af8d39163e"},
    {"data/texture/hp_ao_g_joy_0.wis", 0xde3a807bu, 156889482ULL, 7892ULL, "fe1e98c43d392105d6fb1fed2c19e1cc73e08392cf05d547fef46ef8f88324a3"},
    {"data/texture/hp_ao_g_sad_0.wis", 0xf90b82adu, 156889782ULL, 8297ULL, "340f9dbcc18bda6d421a8ae601dcf2173184938dbf40f0146bcc0672d178b575"},
    {"data/texture/hp_ao_g_sick_0.wis", 0xb90e892du, 156890084ULL, 6692ULL, "7341b265f481a7dc92e8d556168614ac1e67d76ef70fa107f8bc06bb46a84e60"},
    {"data/texture/hp_ao_g_sick_1.wis", 0x7b587864u, 156890387ULL, 6885ULL, "70497efc8c2db595d12232b3720c808efcff4e0117948c0c0969986ef38e4c17"},
    {"data/texture/hp_ao_g_sick_2.wis", 0x0a463590u, 156890690ULL, 6742ULL, "719b4634ab0763cff7878421a5a7b12efe792c518b111170516f156fcb1668d0"},
    {"data/texture/hp_ao_r_base_0.wis", 0x1276c0deu, 156890993ULL, 6357ULL, "e9c4089abaaae5b1ce9d0873f3894dea27805a016a88a6c5514e820e74b7a1af"},
    {"data/texture/hp_ao_r_base_1.wis", 0xe6eb4222u, 156891296ULL, 5865ULL, "602d04811355f346e616a7152cdae4d44d4f4e514d469a7b3b4007d36565872e"},
    {"data/texture/hp_ao_r_base_2.wis", 0xd12b7ce0u, 156891599ULL, 5354ULL, "ca2001f07e1f39e58dd1280acb3aa705501f0a18790265babacfb8ca693b0f29"},
    {"data/texture/hp_ao_r_joy_0.wis", 0x9afd2b5cu, 156891900ULL, 5852ULL, "2e7693277f2bdc6e302aa5a3280d2affe9be51f42ca5c097a6d700b468f49f0e"},
    {"data/texture/hp_ao_r_joy_1.wis", 0x7640aafau, 156892200ULL, 6499ULL, "774a81461a9961e34324a8edb3d5d75320ec48e93ca3883dc1e6ea714f5b2ab0"},
    {"data/texture/hp_ao_r_sad_0.wis", 0x997c808cu, 156892500ULL, 6373ULL, "261e3f55513c5b97ee65ce87f044b6926f9df6a412d7eb48734358da7ff95c20"},
    {"data/texture/hp_ao_r_sick_0.wis", 0x49b9a013u, 156892802ULL, 7050ULL, "ca1749f17bf21684d6d5402764e693845d9d23cc89c0d6fe3e14bfccf576b69c"},
    {"data/texture/hp_ao_r_sick_1.wis", 0x13a4d3e3u, 156893105ULL, 7813ULL, "ff93d1e95d2fac7a0f80a69b068abe22e68be37046e14496e47e2555dd75fa16"},
    {"data/texture/hp_ao_r_sick_2.wis", 0xa4c9ef25u, 156893408ULL, 7555ULL, "1782e3c1b6f3005bfe649ffdf011f5a86f3bb6f2e11655979de5d23ae9fe347d"},
    {"data/texture/hp_ao_s_base_0.wis", 0x84b12913u, 156893711ULL, 5084ULL, "00de31a951d2f171f8b948db0fceb4f5dabade72ef23f51812c3d8702427b10b"},
    {"data/texture/hp_ao_s_base_1.wis", 0xd59b8cf5u, 156894014ULL, 4687ULL, "003c6e63132b8d6aea0101396130f1f9f78971f23b496bab2f3c620921cb4cff"},
    {"data/texture/hp_ao_s_joy_0.wis", 0x84b12913u, 156894315ULL, 5084ULL, "00de31a951d2f171f8b948db0fceb4f5dabade72ef23f51812c3d8702427b10b"},
    {"data/texture/hp_ao_s_joy_1.wis", 0x21e300e4u, 156894615ULL, 5159ULL, "1329ce4642955e76c19c95f3fda76462eb333ee867cb0b0b5ca9beb2ecde0fc2"},
    {"data/texture/hp_ao_s_sad_0.wis", 0xadd726a3u, 156894915ULL, 6045ULL, "a99598685423e325584fd38fd9e8bc077aa2b5231a5c1c5a05b42f69cdcd30c6"},
    {"data/texture/hp_ao_s_sick_0.wis", 0xcfc66725u, 156895217ULL, 5293ULL, "db45085cc8f56f3ac0ccd93d0e1f2cec6183c6f5e2643ae03dabc0c6ec6e2e67"},
    {"data/texture/hp_ao_s_sick_1.wis", 0x715cb1e0u, 156895520ULL, 5672ULL, "762cd9e67bdd56453af3c69ec9d8e380f43d4e308ebaf80b6351d298b134c621"},
    {"data/texture/hp_ao_s_sick_2.wis", 0x89499edfu, 156895823ULL, 6225ULL, "60f188dde32ce38d493807f3db17c6178b423ecd250156742f7bfd499f99ba87"},
    {"data/texture/hp_pz_b_base_0.wis", 0x997e665au, 156896025ULL, 7462ULL, "28d7f0b3a40a98421ad05d98b15d3a130d428907f3f9773ae5b0eff90f9dd15e"},
    {"data/texture/hp_pz_b_base_1.wis", 0x2a8afc92u, 156896227ULL, 7446ULL, "a1670bd239fbc501ef7a0f0531e2421052950b9699293bfbf053d81419a52af8"},
    {"data/texture/hp_pz_b_base_2.wis", 0xc6454b27u, 156896429ULL, 7333ULL, "eca866867d6bb0326f5b1b049d376f3a4720b97087e4346a284226f7a28e13be"},
    {"data/texture/hp_pz_b_joy_0.wis", 0xc6454b27u, 156896630ULL, 7333ULL, "eca866867d6bb0326f5b1b049d376f3a4720b97087e4346a284226f7a28e13be"},
    {"data/texture/hp_pz_b_sad_0.wis", 0x2502933du, 156896830ULL, 9593ULL, "ed397c8d7f40455e778a8f9ffa4791cc86b95afc0f6019c106c29a247004477f"},
    {"data/texture/hp_pz_g_base_0.wis", 0xe4388a7du, 156897031ULL, 9356ULL, "611af0d88ce2bb49a6098dc88229dd4af552a30f7010b0eac493b37aec0e7c4a"},
    {"data/texture/hp_pz_g_base_1.wis", 0xf912bef1u, 156897233ULL, 9405ULL, "165cb592bdd813e81066292f47868b3df56cd451bd9b36db6449d4b28a912b15"},
    {"data/texture/hp_pz_g_base_2.wis", 0x3ba6598eu, 156897435ULL, 9307ULL, "a5f82ec4a8456b0e88799259db09bcb0f0944b56f0ecce410ec943bf219f74c3"},
    {"data/texture/hp_pz_g_joy_0.wis", 0xd7bd9a03u, 156897636ULL, 9224ULL, "dc14c3811a4576768ea1cda8212d83bce807274c4dfccbd02106bf45b4c39427"},
    {"data/texture/hp_pz_g_sad_0.wis", 0xf76ce25eu, 156897836ULL, 10010ULL, "48227906e383e2739e0c9d78e18667564ba05dc524e4bc462d86d1b821219d2a"},
    {"data/texture/hp_pz_g_sick_0.wis", 0x43ba60a3u, 156898037ULL, 8433ULL, "f5a8a5345e07ef14ab052fd1997f8ce90138cdd5a098b344a6cc5cd841bd9797"},
    {"data/texture/hp_pz_r_base_0.wis", 0xd16eeb82u, 156898239ULL, 10119ULL, "abe0e955de39b840adc1a5eff969d218db3f397bad8b506c5f1a90333c5afd71"},
    {"data/texture/hp_pz_r_base_1.wis", 0x64a06603u, 156898441ULL, 9851ULL, "c23051b7dd5afd812394a41e5fb2afc37e5f1831261cc58dd2e4df34dcc9e439"},
    {"data/texture/hp_pz_r_base_2.wis", 0xf62d7a29u, 156898643ULL, 9910ULL, "13e7b5e6169323198ff73245b1d0c11acd8017045ba6c23f5abe4fb32a5b912d"},
    {"data/texture/hp_pz_r_joy_0.wis", 0xb7379273u, 156898844ULL, 9844ULL, "8c75db1fa3de215a75a136190835851a502257c5b827e734efeed5ed61800e6d"},
    {"data/texture/hp_pz_r_sad_0.wis", 0x1863b5fcu, 156899044ULL, 11612ULL, "1e219825239ed0b73888961cd8cdfca53f696d059fcc0558cb3aa334d4b65017"},
    {"data/texture/hp_pz_r_sick_0.wis", 0x788d8e14u, 156899245ULL, 9876ULL, "ea39a474bb4802a741d3031122a4cf672a3e6e68170bba8fd85f80c896798763"},
    {"data/texture/hp_pz_r_sick_1.wis", 0xdd56eb80u, 156899447ULL, 8829ULL, "2efa79449a8137f42a4f819e5348562e6356834e971ca5a331c188542abf27d1"},
    {"data/texture/hp_pz_s_base_0.wis", 0xea29a3b1u, 156899649ULL, 8594ULL, "b68a43e1743ff3ad022df2db5763ee46a5ea882e37516d894e7e046707e26319"},
    {"data/texture/hp_pz_s_base_1.wis", 0x67c2ab6du, 156899851ULL, 8594ULL, "b67bb481917ac163e5835e746b3e8fb0994fbcff3ca5d9f193a32125ca3c27b0"},
    {"data/texture/hp_pz_s_base_2.wis", 0xd96f0ab6u, 156900053ULL, 8580ULL, "714e637a424315282460d1bb5e0a864571c54a38b088095db964e18fc9089b62"},
    {"data/texture/hp_pz_s_joy_0.wis", 0xea29a3b1u, 156900254ULL, 8594ULL, "b68a43e1743ff3ad022df2db5763ee46a5ea882e37516d894e7e046707e26319"},
    {"data/texture/hp_pz_s_sad_0.wis", 0x983aff9cu, 156900454ULL, 8994ULL, "a602c7db802ceb41525d74b9e846251cd2fe0adff14b25d9e4aa50475600bbbf"},
    {"data/texture/hp_pz_s_sick_0.wis", 0xec82bb2fu, 156900655ULL, 7835ULL, "7858142ca6d6bc613ad38d3ddc552707b535e7a386dbaa8c5f27ac242eda481b"},
    {"data/texture/hp_to_b_base_0.wis", 0x2e74aed2u, 156900857ULL, 6896ULL, "3c9efb39e748d08de2b7fb906e3aa40cfa9f7ca6781e933d7290c4af24086f0d"},
    {"data/texture/hp_to_b_base_1.wis", 0x1f7633bcu, 156901059ULL, 6602ULL, "aaa699ca7ac70193080782e8837374c8f66fe5d38ac140ff5307f7cf1a3a6795"},
    {"data/texture/hp_to_b_base_2.wis", 0x04f43b7bu, 156901261ULL, 6942ULL, "cd2a5c1dfbcf5477d6b9fab3be08b9f631de3763f0d5f9edf26728d75bff6d4d"},
    {"data/texture/hp_to_b_joy_0.wis", 0x9aae74cbu, 156901462ULL, 10215ULL, "496ee1e3d95bb4443b387b373681959c3f0f845f99f99fdcd477c9801d0c4069"},
    {"data/texture/hp_to_b_sad_0.wis", 0x5719e042u, 156901662ULL, 7070ULL, "3da31b8a5d0bcd9b5cf90ac83ddc7039f8209d4069566093440e26765ae3fe44"},
    {"data/texture/hp_to_g_base_0.wis", 0xd26a3d43u, 156901863ULL, 6206ULL, "fd7e609a6c6e74cbaa9b6561a39406fdbbad8ea1e064b0ecad124d2495272289"},
    {"data/texture/hp_to_g_base_1.wis", 0x28b6ce2fu, 156902065ULL, 5715ULL, "142ff4826592bd8c362a59104c1bd38797de031558b8f4eb91ac1822c6980cca"},
    {"data/texture/hp_to_g_base_2.wis", 0x9d67da66u, 156902267ULL, 5480ULL, "c91992ce53c381a9672fcafe0f6a0665a0435fbfbbb511afe15e7426cdf087e7"},
    {"data/texture/hp_to_g_joy_0.wis", 0x68c43e2cu, 156902468ULL, 6241ULL, "611ae23add9ab4a47642df49b20831c43c7627ef82a8870650fcdca3f251b43d"},
    {"data/texture/hp_to_g_sad_0.wis", 0x66740f9eu, 156902668ULL, 6262ULL, "e917e91eb615d9635a3705a2214e46832043686b7345b38dccd6df81525c5b5d"},
    {"data/texture/hp_to_g_sick_0.wis", 0x28a3dfd3u, 156902869ULL, 6210ULL, "724b60b4460febc3136a990cda0568591cfad899ef8fe2a619985df01fa0eddf"},
    {"data/texture/hp_to_g_sick_1.wis", 0x044a9fc2u, 156903071ULL, 6211ULL, "093c5b67907f45bddfdc0d7a8afb3f93bda6ce0acad9952102f6c819c439aa79"},
    {"data/texture/hp_to_g_sick_2.wis", 0x90b7c311u, 156903273ULL, 6265ULL, "62c1a7c48ec48c0d3ffa52d79077ce3ee9b98cd6cd032cd065cd508a91098fac"},
    {"data/texture/hp_to_r_base_0.wis", 0x8adb428bu, 156903475ULL, 9883ULL, "05f61f58e209b94f95c699936b643065c760edc0074b17e0a3d7ecdec16accb9"},
    {"data/texture/hp_to_r_base_1.wis", 0x1af27e3cu, 156903677ULL, 8905ULL, "31cd41f2862b816dacba6b87f96de67dc6c883ead7d3a71782a1ad2ee942ff34"},
    {"data/texture/hp_to_r_base_2.wis", 0x8173bf21u, 156903879ULL, 8459ULL, "e87e298a2b335836413c4c0eec838cf42c2563ecafef7e926c051f2b4452b361"},
    {"data/texture/hp_to_r_joy_0.wis", 0xbb29d6beu, 156904080ULL, 11917ULL, "fdca0420ad8f7c959c404463ec551b6c10a0da878d0e9c47f064ea135534c60f"},
    {"data/texture/hp_to_r_sad_0.wis", 0x32a8e7a4u, 156904280ULL, 11226ULL, "071875a3c448ceeef909fbe0249b571783c4fde28f09eb5a19ccee8dc20beff5"},
    {"data/texture/hp_to_r_sick_0.wis", 0x1bbd77a3u, 156904481ULL, 11161ULL, "350c7af55a482736ba254fa1f7d6dcc3649cea87f49aee3ef868648aa362ed64"},
    {"data/texture/hp_to_r_sick_1.wis", 0x12e5c722u, 156904683ULL, 10922ULL, "ac6358eb0554e1340c7ff8dbd14a083f1e056cd391f0bbac30515cd253d75f09"},
    {"data/texture/hp_to_r_sick_2.wis", 0x0f759cc5u, 156904885ULL, 11395ULL, "a595a328847aff63572c3c42d8455a56f5a2e92d32be206e8be77c894d05215a"},
    {"data/texture/hp_to_s_base_0.wis", 0xaf72766fu, 156905087ULL, 7186ULL, "e0c88bdf67f65af1cc10d9fa8b540d851b17052ca62eb181f6e55b1c71c03fdf"},
    {"data/texture/hp_to_s_base_1.wis", 0x27713bc3u, 156905289ULL, 7276ULL, "0a23a666d94a9f573d49d68ff7046634bc9143359fddd75c88185db5e80ec441"},
    {"data/texture/hp_to_s_base_2.wis", 0xae15ec68u, 156905491ULL, 6775ULL, "f16484b38cb1d3b9398b183f0ab134a4e118e51fd28079ae0af28b0e23f1ceb0"},
    {"data/texture/hp_to_s_joy_0.wis", 0xd1b5e6adu, 156905692ULL, 8321ULL, "89f45dd87201ed9556a17298e3996a4d75ad175341087e83dc1e4b527abdcb4a"},
    {"data/texture/hp_to_s_sad_0.wis", 0x22d24c08u, 156905892ULL, 7856ULL, "7c02f07232bb75c41f733045ef0500a5a58bdb7ea8be1e6c24d4ca6bd9763b1e"},
    {"data/texture/hp_to_s_sick_0.wis", 0x005de561u, 156906093ULL, 8856ULL, "cd9015a89b02654aecdba0af0d21e4bb94d76b7d15d0f31449f469aaecb51fcf"},
    {"data/texture/hp_to_s_sick_1.wis", 0x2e5d6766u, 156906295ULL, 8668ULL, "2e1824fdb31b2b4f67c62abebf976be5f18bcd2bb0609064221511839cb434d3"},
    {"data/texture/hp_to_s_sick_2.wis", 0xd80f27c7u, 156906497ULL, 8684ULL, "358fe6d4f3d1cddc9bb26391a9ac5031c1303ccec4203aa7a3b80e4b994f89ad"},
};

static constexpr U32 WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT =
    (U32)(sizeof(WASM_JIT_AZROK_FACE_WIS_MANIFEST) /
          sizeof(WASM_JIT_AZROK_FACE_WIS_MANIFEST[0]));
static_assert(WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT == 113u);

static constexpr WasmJitAzrokFaceWisHashIndex
WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[] = {
    {0x013b1a98u,47u},{0x01cbed8cu,77u},{0x043310cdu,44u},{0x05b1fdc1u,97u},
    {0x09779966u,13u},{0x0bafc74bu,31u},{0x0f78b808u,82u},{0x0f944ac3u,67u},
    {0x0fb8559fu,15u},{0x10c3a7f0u,57u},{0x15169028u,88u},{0x1623a5dau,73u},
    {0x1631b50du,96u},{0x16fe663eu,39u},{0x17450135u,16u},{0x179403abu,94u},
    {0x1b129b05u,7u},{0x1ce5b5ffu,99u},{0x1fc15414u,2u},{0x246389a5u,74u},
    {0x26611df1u,78u},{0x2ccef815u,108u},{0x2cdf09c8u,71u},{0x313ab3eau,107u},
    {0x35f2d0cfu,75u},{0x38fe2f1fu,64u},{0x3aac2478u,60u},{0x3beb4e4au,62u},
    {0x3d94d62fu,80u},{0x3e1542fbu,46u},{0x4261b7c1u,48u},{0x434295d4u,23u},
    {0x48957b98u,105u},{0x48bb38d1u,33u},{0x48f0d7b1u,17u},{0x4b930843u,5u},
    {0x52be71e0u,8u},{0x553528eeu,91u},{0x56f6944fu,86u},{0x585ccd91u,84u},
    {0x58e7d8d6u,27u},{0x58ed89aau,50u},{0x598399b6u,68u},{0x5a7b0351u,37u},
    {0x5aa71df2u,12u},{0x5ae1e7a4u,40u},{0x61c2a8c5u,4u},{0x64b287b1u,92u},
    {0x6782dbf3u,52u},{0x68088516u,0u},{0x6c04d4fbu,93u},{0x6d2a042bu,72u},
    {0x70a186dau,100u},{0x7139640au,28u},{0x714fa5afu,76u},{0x742e142au,81u},
    {0x74dbe80fu,35u},{0x763d08b0u,54u},{0x793b0122u,59u},{0x797144efu,19u},
    {0x79ff22dbu,61u},{0x7b1ad103u,11u},{0x7e63bf38u,101u},{0x805d175cu,21u},
    {0x80935fdcu,111u},{0x8250aa17u,26u},{0x83c9c380u,104u},{0x8565e44cu,89u},
    {0x856fa3fbu,106u},{0x8a34fbe6u,42u},{0x8bc2eb38u,30u},{0x92480689u,14u},
    {0x930b927eu,98u},{0x98b7495du,49u},{0x9ff3d1c4u,38u},{0xa41d958eu,24u},
    {0xa8828222u,83u},{0xa9160e80u,6u},{0xac14981au,43u},{0xb07b4e0eu,70u},
    {0xb23abbeau,9u},{0xb43b9de7u,41u},{0xb5454468u,32u},{0xb575d233u,58u},
    {0xb72d17aeu,79u},{0xc092cfafu,3u},{0xc8e804bbu,51u},{0xca516bbdu,29u},
    {0xcbf69954u,69u},{0xcd61a94eu,85u},{0xd35b0cb2u,66u},{0xd742686eu,18u},
    {0xd7490948u,95u},{0xd852ec01u,22u},{0xd8893481u,112u},{0xd988c58eu,34u},
    {0xdb184040u,10u},{0xdc1e4408u,45u},{0xde97cfb0u,53u},{0xdf13dd85u,65u},
    {0xea0d7f32u,102u},{0xebc9acbau,56u},{0xec0d0decu,36u},{0xed0c3b3fu,20u},
    {0xed4283bfu,110u},{0xee045df9u,1u},{0xf14a95cau,87u},{0xf590e175u,63u},
    {0xf5c65005u,103u},{0xf764056fu,90u},{0xf96a36bfu,109u},{0xf9cfeed4u,25u},
    {0xff2814f3u,55u},
};
static_assert(sizeof(WASM_JIT_AZROK_FACE_WIS_HASH_INDEX) /
    sizeof(WASM_JIT_AZROK_FACE_WIS_HASH_INDEX[0]) ==
    WASM_JIT_AZROK_FACE_WIS_MANIFEST_COUNT);

#endif
#endif
