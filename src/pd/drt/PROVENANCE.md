# drt: provenance

Upstream: OpenROAD at commit ead3236e56d57362a8dc8b25658fb41a191eba17
(the same commit that supplied the in-tree OpenDB), imported as
EOS's own code under unit P5 (detailed route) of the place-and-route
phase: TritonRoute detailed routing (FlexDR, FlexTA, FlexPA, FlexRP,
FlexGR, FlexGC), driven by the EOS-owned runner in src/pd/route.
The pin tree itself is never modified; the files below are copies
into this module, with the port adaptations listed in PATCHES.md.
No git submodule, no external process at runtime.

## Pin verification (run at import time, 2026-11-02)

The local pin tree at ~/workspace/openroad-ead3236 is a tarball
without .git, so the pin is verified two ways, not asserted: (1)
its BUILD-RECIPE.md names OpenROAD
ead3236e56d57362a8dc8b25658fb41a191eba17 (the ORFS v3.0 pin);
(2) sampled SHA-256 hashes from that tree match the merged P2
PROVENANCE exactly (gpl replace.cpp, gpl timingBase.cpp, grt
GlobalRouter.cpp). The drt files below come from that same tree.
No other commit was substituted.

## Licence audit (run at import time, 2026-11-02)

Every file in the import set was grepped for licence text
(GNU/General Public/Affero/Mozilla/Eclipse/Apache/MIT phrases)
and the BSD headers were read (BSD 3-Clause, UC Regents 2019).
Result: BSD-3-Clause only. No copyleft text was found in any
imported file, and no "GPL" string appears anywhere in the set.
167 of the 168 imported files carry the BSD header; the one
exception, src/db/tech/frConstraint.cc, carries no licence
header of its own (its companion frConstraint.h carries the BSD
header, and the file contains no licence text of any kind).
The upstream LICENSE file (BSD-3) is copied as LICENSE in this
module. The Tcl factory (MakeTritonRoute.cpp), all .tcl/.i
surfaces, and the upstream test directory are not imported. The
Flex*Graphics files are imported but inert under the dbsta gui
stub (Gui::enabled() is false, so no descriptor is registered
and no drawing runs).

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/drt) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/triton_route/TritonRoute.h | src/drt/include/triton_route/TritonRoute.h | f9afae61b8dbd023ea5fbdb24a15d73e46a144059982e3ccbf2a8957b1efc33c |
| src/DesignCallBack.cpp | src/drt/src/DesignCallBack.cpp | 49b631e9bdb7f127433c71a66b67fa81623d13ea1ebb31f372eb1bacba700bd0 |
| src/DesignCallBack.h | src/drt/src/DesignCallBack.h | b08bcc71c2367883f18d619f5e93dfbc31e78fa4a6c6162efa869bcf6942a7da |
| src/TritonRoute.cpp | src/drt/src/TritonRoute.cpp | 75ca8d426c701ca57cf3fa9b966476569f213adbf1257a36d747eef34a34d4d2 |
| src/db/drObj/drAccessPattern.cpp | src/drt/src/db/drObj/drAccessPattern.cpp | 26617489a6764489f7b4809e1b4b8aed51e171fd3c12fb5a8cbc7e64d782b00a |
| src/db/drObj/drAccessPattern.h | src/drt/src/db/drObj/drAccessPattern.h | 4bf060270ff7c245e876423f5aa1dd2c2398b8be06f824310bbb28e48bfca6b9 |
| src/db/drObj/drBlockObject.h | src/drt/src/db/drObj/drBlockObject.h | 369517dc5070391ce94e8efcf6a72bb536e15a01ae5053dd64a7af60a21118c0 |
| src/db/drObj/drFig.h | src/drt/src/db/drObj/drFig.h | 2898d3a6a3dfdefe5a12f5dc50301f0cfbb5639762bfcd622e1cb74937e181db |
| src/db/drObj/drMarker.h | src/drt/src/db/drObj/drMarker.h | 420c1183d76cf1a3d04ddeaee77a5ecb00bdfba64a128c7a80367fbd7f1e787a |
| src/db/drObj/drNet.cpp | src/drt/src/db/drObj/drNet.cpp | 4831ea98a9676bb9e7239a047bc49d102aa07704669fd6fb5d08100fd42cf4b6 |
| src/db/drObj/drNet.h | src/drt/src/db/drObj/drNet.h | e1bd1b58e32f41a897cfbf38e69d22c5cc83cd6dc1c961beae63b098160779d4 |
| src/db/drObj/drPin.cpp | src/drt/src/db/drObj/drPin.cpp | acac48b1e9df1e9eb8168c362b460997bed6a6cefeaaddcfd8b0ed7205ceb514 |
| src/db/drObj/drPin.h | src/drt/src/db/drObj/drPin.h | 966e4c63aeec242e5ee47cb6e6b387e82f33d0e47ab1d10554aab76d027cd5ee |
| src/db/drObj/drRef.h | src/drt/src/db/drObj/drRef.h | b2c5ad67a90a1fce63eae50829c2f7bf01d32cd2b8bc148fe3138ab85b0cc0a6 |
| src/db/drObj/drShape.cpp | src/drt/src/db/drObj/drShape.cpp | 6ed6ffb34078d199825c8dd616492c3dd6b7a6e2b6bdae29f4ea6be9d6b2d2e2 |
| src/db/drObj/drShape.h | src/drt/src/db/drObj/drShape.h | 466e434d2e1a4abd7f08fd02794d9dc832384cd86877abc9e63d475390cd3be7 |
| src/db/drObj/drVia.cpp | src/drt/src/db/drObj/drVia.cpp | 50f9b8f2ab274a386e908ddef2559f9796d0319339007771ac62536a3b040665 |
| src/db/drObj/drVia.h | src/drt/src/db/drObj/drVia.h | 442f8bbe0971f8e1b365ee90503b02b8dd765594653583d6bda27968a88c1831 |
| src/db/gcObj/gcBlockObject.h | src/drt/src/db/gcObj/gcBlockObject.h | c352f87a6885a924533cf2d50f9e6673711567f74fb65e1153eaa611b012837b |
| src/db/gcObj/gcFig.h | src/drt/src/db/gcObj/gcFig.h | d6f8bd74d7b0c79ee3865d22db352b68b61594a027ce724f80d9040094f14c79 |
| src/db/gcObj/gcNet.h | src/drt/src/db/gcObj/gcNet.h | 6b66d9d396c429152e120493bc41326a9f4ae7b5cb7c6885a162e75a51564eca |
| src/db/gcObj/gcPin.h | src/drt/src/db/gcObj/gcPin.h | dae2cd9788c29ab53798d8fe6f348cf1be5690c6211c540dc7407911500e837d |
| src/db/gcObj/gcShape.h | src/drt/src/db/gcObj/gcShape.h | 75ccb61466d1b4161be2c08528a44240aae04299a97de3e04ea1f7d055a71f48 |
| src/db/grObj/grAccessPattern.h | src/drt/src/db/grObj/grAccessPattern.h | d2137f6c67ee520789b98d0e47e93b838a15fdae8508f573dd1a3d91db7014c7 |
| src/db/grObj/grBlockObject.h | src/drt/src/db/grObj/grBlockObject.h | 3178008008ae447686033abd8cc8fd861fe2d6831df99c55a0fe3b4caf5a033b |
| src/db/grObj/grFig.h | src/drt/src/db/grObj/grFig.h | 3139d47b0849ed1a71fd5065406c269c03f7d5f016b533f6515c9e3a3f89e9f9 |
| src/db/grObj/grNet.h | src/drt/src/db/grObj/grNet.h | f222fe45a477998e6326e55c0067305112a5045f9387864725b495d665b4ca5a |
| src/db/grObj/grNode.h | src/drt/src/db/grObj/grNode.h | 1b66cbfc7f77d43a08941cc0939a9611995d64e9ac6e5faa1ad5fc038f01dbda |
| src/db/grObj/grPin.h | src/drt/src/db/grObj/grPin.h | ba3f3f3e38982f233be9167d9aa902d467b0e5bb86bf53fdc78fa1afcf2a6e2e |
| src/db/grObj/grRef.h | src/drt/src/db/grObj/grRef.h | dbebc2893eaf7d9f0afe899df9c14753ff413ea1375072870edb7c4549597960 |
| src/db/grObj/grShape.h | src/drt/src/db/grObj/grShape.h | 04668ebf4b0bd6100a06c1272ca855be595c9105563fef828b835cc802cc12af |
| src/db/grObj/grVia.h | src/drt/src/db/grObj/grVia.h | cc9d157b1daa30fba101cdb6b3ac265114a1a7512b4d2de0beeb97e7f18635a4 |
| src/db/infra/frBox.h | src/drt/src/db/infra/frBox.h | ec53eb1ff880a987e7e60ca78d415283dea6748a171d99e83a56d56e508aed10 |
| src/db/infra/frPoint.h | src/drt/src/db/infra/frPoint.h | cf01ebd38a674340ffa011035d70f1a9ffe1453872cc1bc365ce6a874b9484f1 |
| src/db/infra/frSegStyle.h | src/drt/src/db/infra/frSegStyle.h | 1e6af13f48558c96bc5da4bfabf37b70da529031243bf6665bb362db9e1d129c |
| src/db/infra/frTime.cpp | src/drt/src/db/infra/frTime.cpp | 18c644e6a24a26ccbc58432a0187cd4cbd1c973f4dedeec164f7fbad3115c9ea |
| src/db/infra/frTime.h | src/drt/src/db/infra/frTime.h | 5124baf82e381a37aea805c3b2c67c53de8e98d893aa070005df1b96a0fa9dc9 |
| src/db/infra/frTime_helper.cpp | src/drt/src/db/infra/frTime_helper.cpp | 89b363cc6051e7803d39d63fad55e27bd1aee3b0a46b0a3dc7b21d273751eba2 |
| src/db/obj/frAccess.cpp | src/drt/src/db/obj/frAccess.cpp | e70c4eeda993912119ee653f06b8b150d3d4c7ec44bf0fc58755cff0f44d271e |
| src/db/obj/frAccess.h | src/drt/src/db/obj/frAccess.h | 1de94a3957a7a9d3ce7447d4a4ab69b045207ab1ecccfe4a1b8342a8b9e1415e |
| src/db/obj/frBPin.h | src/drt/src/db/obj/frBPin.h | 498721e4efd94e16db73a427d4af3b72c964ed7188241356adf6499764c1b1df |
| src/db/obj/frBTerm.h | src/drt/src/db/obj/frBTerm.h | 366dc9c8dbe6bb7d82ef8382384aef1a0b2c8c71a78b8d92ed283e3d1468dd57 |
| src/db/obj/frBlock.h | src/drt/src/db/obj/frBlock.h | d089d795bc96f7c20a1405ae5bca673f14f5c165d5bb94c4841eacd99fcc9acf |
| src/db/obj/frBlockObject.h | src/drt/src/db/obj/frBlockObject.h | 82e59f542f10d3067bf74d30a1478991228f96b240b938c15516092c2a4eaedf |
| src/db/obj/frBlockage.h | src/drt/src/db/obj/frBlockage.h | ea044b48ae25bf72ce79a8657d5160426f1f67db5e6fd22cd955418bcfa21ca9 |
| src/db/obj/frBoundary.h | src/drt/src/db/obj/frBoundary.h | 4481e47f2dfea12cce0b5f2be2369243aa5f49b09501aa0f2e768e745a21e81d |
| src/db/obj/frFig.h | src/drt/src/db/obj/frFig.h | 94c9cfe2831fe5e193af8e4e2c58daf0bc1ec6d53985652f5880d84d5ddd8424 |
| src/db/obj/frGCellPattern.h | src/drt/src/db/obj/frGCellPattern.h | 9219a0e29c028ea3a61bdeef3ebcfb183fc084bd90c295cf7fada3002f9b5eac |
| src/db/obj/frGuide.h | src/drt/src/db/obj/frGuide.h | d44437add75639e4a010118911e2da3f465bdcc810c00e45976627cfd9a118c6 |
| src/db/obj/frInst.cpp | src/drt/src/db/obj/frInst.cpp | 567cf799a1bdb62198b508d6342d1ccd6f4beb697c9f7332da72d3212e58c3b1 |
| src/db/obj/frInst.h | src/drt/src/db/obj/frInst.h | 5cf642dacf85989253b6c643a4a2225678f144370334e362b2747d66e31afaa0 |
| src/db/obj/frInstBlockage.h | src/drt/src/db/obj/frInstBlockage.h | d4c78c6450727a15ced847453b96e899fd5f81d77ca72cd15d7c219916f3b33f |
| src/db/obj/frInstTerm.cpp | src/drt/src/db/obj/frInstTerm.cpp | 283dd9faf7e27f249165b4f6419b6ff396c6dbb134e2edd35b26fb8693e984ed |
| src/db/obj/frInstTerm.h | src/drt/src/db/obj/frInstTerm.h | 2c3ad245d2c34a59359da227fb679ec31cb5a4815a3697ba26b098242be18921 |
| src/db/obj/frMPin.h | src/drt/src/db/obj/frMPin.h | ecd21cc62dd5ba56e52bc26df6ac23bb99f7d5fdc029b78cb0ca675722702550 |
| src/db/obj/frMTerm.h | src/drt/src/db/obj/frMTerm.h | db2e9f107ae4441e628ff22b03e5d430004c948d718c6e7cee6979a24a5cc8fa |
| src/db/obj/frMarker.cpp | src/drt/src/db/obj/frMarker.cpp | d980c607df57f0ae9ef3bf260c8a4d0b5433b04579637860181565234295e4df |
| src/db/obj/frMarker.h | src/drt/src/db/obj/frMarker.h | f4e063fe76f2c0ef40f48e9a308c74f61ed34c9c6bd6dccf365ca48ecaf17d8c |
| src/db/obj/frMaster.h | src/drt/src/db/obj/frMaster.h | 3dd8b303d3c2d5394d838ab80d4c621b699f20741eff5c13637c00fc73cf53fd |
| src/db/obj/frNet.h | src/drt/src/db/obj/frNet.h | 83d2842c2f067aeb004e213e56fefbb0842d49985ee14c1f378dd1b2f31d5a77 |
| src/db/obj/frNode.cpp | src/drt/src/db/obj/frNode.cpp | fe6a08281244b67b3721187d65cdbeb9b0b17ae1a145dd29a12fb306a0375e3d |
| src/db/obj/frNode.h | src/drt/src/db/obj/frNode.h | 25735ad7af83282925f263b0eefe146981a67e948783a9295483b2b158610d7c |
| src/db/obj/frPin.h | src/drt/src/db/obj/frPin.h | e82281c9e0839ddaee5b40e59e3c6605150e3f50164186df29315a902338f7a1 |
| src/db/obj/frRPin.cpp | src/drt/src/db/obj/frRPin.cpp | d4e39712a6763e1a6e7553b047238119a91f5969e2282dc10adb471d755d0222 |
| src/db/obj/frRPin.h | src/drt/src/db/obj/frRPin.h | ac2cc9216fcbc037ad6498b793486e07845a0802e98e4ff230888984f6c7d643 |
| src/db/obj/frRef.h | src/drt/src/db/obj/frRef.h | d399a431b1411ff5c1e84ad6f5dc4c58c8c069e8492553b413b3aeb23c193107 |
| src/db/obj/frShape.cpp | src/drt/src/db/obj/frShape.cpp | 4b9a603a98cef80d5787544225e5c5cee70dbac05d62e33544b31b3408f44eca |
| src/db/obj/frShape.h | src/drt/src/db/obj/frShape.h | 2b2ad3b71eb135913deda7181823a0cca264f26feb2ec82907968d5821d6d1ed |
| src/db/obj/frTerm.h | src/drt/src/db/obj/frTerm.h | cecba1ce5b163dfd15e0f2e25a1415785a91da974cc3be06683de0235309fa8c |
| src/db/obj/frTrackPattern.h | src/drt/src/db/obj/frTrackPattern.h | 4a24ee03f002c220bf2d7d54c4b95c87d960541146185ea51d27605ccd0efc9a |
| src/db/obj/frVia.cpp | src/drt/src/db/obj/frVia.cpp | e70bf4e5ade5ddce331dcfe9c6ad51c057458aad76d23b112748ccbd03af6e99 |
| src/db/obj/frVia.h | src/drt/src/db/obj/frVia.h | 8dbcd0d83f599f564b8b4d425ff40b9d941a0fbd097d318cdfe976eac8eab2a9 |
| src/db/taObj/taBlockObject.h | src/drt/src/db/taObj/taBlockObject.h | aa70afbb406590745f89a7792922f80c454a7d738e2fa6a610210cf917450c71 |
| src/db/taObj/taFig.h | src/drt/src/db/taObj/taFig.h | 75a5913d2db16cbac221bd00c631902dc4644af41532115359f7fed24250503a |
| src/db/taObj/taPin.h | src/drt/src/db/taObj/taPin.h | 3f623dc8edebf96644a9c49ec16c3c5ecacbacfaa4bf59c4a2763b177bf3639b |
| src/db/taObj/taShape.cpp | src/drt/src/db/taObj/taShape.cpp | ad2e132e5dca8ae931354d2859a716b8435e08dbaef462b90a6d34321b75f131 |
| src/db/taObj/taShape.h | src/drt/src/db/taObj/taShape.h | 7ff2088b8213add9cc4e3aa7e7b1d669ba54189c3564bea5e2dfcf164a63f7bb |
| src/db/taObj/taVia.h | src/drt/src/db/taObj/taVia.h | f1a658dde952a8d8e966693b098c5a4642c9533e00cc9002c7dbc51e6646794f |
| src/db/tech/frConstraint.cc | src/drt/src/db/tech/frConstraint.cc | 6c98e09f579aa4d0cce3975586c11383e3ebdd83d98cc3ccb35acb3f65d158d0 |
| src/db/tech/frConstraint.h | src/drt/src/db/tech/frConstraint.h | 10fb64c4c7eaf1b0e4fc9376ab3808d809c87bc91e81eeba4e084912ef85526f |
| src/db/tech/frLayer.cc | src/drt/src/db/tech/frLayer.cc | 093e0ab93aadcb61bd089de10429a53d2cb3d72dcbe90582fab86fc51a4090e1 |
| src/db/tech/frLayer.h | src/drt/src/db/tech/frLayer.h | 69130c1fa7dcefe5bf4918df71b5d65d3c5cba370d9f82821055c538c1b3e205 |
| src/db/tech/frLookupTbl.h | src/drt/src/db/tech/frLookupTbl.h | 18fd496971db6ea23018bb9dacc3c50ec5839d7af7a3b4bb176df9bd742aa6d7 |
| src/db/tech/frTechObject.h | src/drt/src/db/tech/frTechObject.h | b1c04984b9a455c051131c1236ac3bb99b0f119d743fb3ffbcb8792c3ad77196 |
| src/db/tech/frViaDef.h | src/drt/src/db/tech/frViaDef.h | 2b375f5a4409baaa9c7efbefaac6fd8d4d9aefb9f6d25416f8550a46f0931bbc |
| src/db/tech/frViaRuleGenerate.h | src/drt/src/db/tech/frViaRuleGenerate.h | 461b1fb6ffe46a1056cbe5e6f0f1b0b4630cc00b1ff54bad55e4f65873b59c63 |
| src/distributed/PinAccessJobDescription.h | src/drt/src/distributed/PinAccessJobDescription.h | 0862c98387ca710a6548aef255def0a8052149324ed8fa305e9ad855183123f1 |
| src/distributed/RoutingCallBack.h | src/drt/src/distributed/RoutingCallBack.h | 8ce69832b3a3dbe0f33a54c7761694f7aa0693a9443639cb05bd1b889f13a523 |
| src/distributed/RoutingJobDescription.h | src/drt/src/distributed/RoutingJobDescription.h | 82a083fb365f212576c072c1e08abe0eb1fc20abb84f4b0e1949799417fd65b2 |
| src/distributed/drUpdate.cpp | src/drt/src/distributed/drUpdate.cpp | edce0775731dd08f20440d3296b71c3e23f88967b3f159e7f1d673449fbbe175 |
| src/distributed/drUpdate.h | src/drt/src/distributed/drUpdate.h | ccf5434d6b9e738847c23cd6df127db2bacc0cdbdba68a9e1ddd0baef45fe1ee |
| src/distributed/frArchive.cpp | src/drt/src/distributed/frArchive.cpp | d2fe5141f2da208154ebed7a537f405c185c3f0383c526f8726ff90badd93d97 |
| src/distributed/frArchive.h | src/drt/src/distributed/frArchive.h | 41cfe8609eb9c0a3095db5a22eb266a767f7075da1de1a435a0ebc151f6b0e33 |
| src/distributed/paUpdate.cpp | src/drt/src/distributed/paUpdate.cpp | 6262280833a8b445a98bcb39a928303bc1c43543f2d1fdf7b56a00c0a8d237c6 |
| src/distributed/paUpdate.h | src/drt/src/distributed/paUpdate.h | 935257539a66ca7846ff911eb1b3e97c8f84681e59d76238c75954c11da36e2d |
| src/dr/FlexDR.cpp | src/drt/src/dr/FlexDR.cpp | b96391afa87a3eb7cac807c3c2a5bcc49653a89c96b6ff977601dc5e0c147497 |
| src/dr/FlexDR.h | src/drt/src/dr/FlexDR.h | 92a75a34980b8e6f9dc3274e17934fe6705fb974466d27f37c8b7278442078c2 |
| src/dr/FlexDR_conn.cpp | src/drt/src/dr/FlexDR_conn.cpp | 6e34b46c9acfcf5b2ae33cf06e382cbad868c192bb7041615586911351949e23 |
| src/dr/FlexDR_conn.h | src/drt/src/dr/FlexDR_conn.h | ee3de20437d67a369565eeb405b26546029e15168432dca770c701de6f2cfe51 |
| src/dr/FlexDR_end.cpp | src/drt/src/dr/FlexDR_end.cpp | 50f9807a4d16ea060a114fbe71ff5bf5de8c316b411e38574b12e53c926ed144 |
| src/dr/FlexDR_graphics.cpp | src/drt/src/dr/FlexDR_graphics.cpp | cff9e6962430c81e89b36d4000dd4b71a12dd4bb2ec3333335e9ca9cc3cdf299 |
| src/dr/FlexDR_graphics.h | src/drt/src/dr/FlexDR_graphics.h | 0d8fc034fddee4cd3e1e816824fe276c8818956eb9343a803283fe042aa1039a |
| src/dr/FlexDR_init.cpp | src/drt/src/dr/FlexDR_init.cpp | 16bc757e779370f428a5b7a1e88cae27519292279ae025fba1d22a0d708b84dc |
| src/dr/FlexDR_maze.cpp | src/drt/src/dr/FlexDR_maze.cpp | 82778ab8a8baae08f1e24eda2ceb7031b5d4e1a669693d6357ba0369fadcf540 |
| src/dr/FlexDR_rq.cpp | src/drt/src/dr/FlexDR_rq.cpp | 314f0ffd0da9cb01548f828a0b9eabf3854c9e19daf0c88d762f148ce3b2d91c |
| src/dr/FlexGridGraph.cpp | src/drt/src/dr/FlexGridGraph.cpp | b2b7f48e7b0ff4026f3d1823d81e5ca5c846d9ffd69bc900ba464d2b78a92f75 |
| src/dr/FlexGridGraph.h | src/drt/src/dr/FlexGridGraph.h | 7a03ff2043004b954c35c01fc46fb9d05aecab09a8f5423c8e2257412797bf35 |
| src/dr/FlexGridGraph_maze.cpp | src/drt/src/dr/FlexGridGraph_maze.cpp | 410b56c5e6a2212eacdfd810ef07a114aa66023fad15dd9d67acaf967800e525 |
| src/dr/FlexMazeTypes.h | src/drt/src/dr/FlexMazeTypes.h | bfd3dd925968c49d9668fa9ad5250d5cda5b2abf5c1d49868d408d8914a7ad7f |
| src/dr/FlexWavefront.h | src/drt/src/dr/FlexWavefront.h | 62908f7072d0531d91211687a26bd379564139a6bf70a263bd0fe9b52dfb78ec |
| src/frBaseTypes.cpp | src/drt/src/frBaseTypes.cpp | 00db23acfb2f7ac6ef87bac63628a6ce2fd433c9bcdb2dc5abaec9f9b5c1bccd |
| src/frBaseTypes.h | src/drt/src/frBaseTypes.h | 5eef728db0f12644f7fa81ba812bee8106aa81e3b464be54ad1c6883306ca654 |
| src/frDesign.h | src/drt/src/frDesign.h | d620c414050b10dcc9e78be8643f209bfea2a289e16665a8b34e494982ed9b66 |
| src/frProfileTask.h | src/drt/src/frProfileTask.h | 7cacbc6c837654379e23cbf0fb4fe663fdeb3d4552b4371744dc218ebc45f101 |
| src/frRTree.h | src/drt/src/frRTree.h | 2dd044d3f7b5867ed6c46ba47e994b8db1967eec641e2bf2d08f9da61a17000a |
| src/frRegionQuery.cpp | src/drt/src/frRegionQuery.cpp | 50449e00ab7523915cf8aef584fa430332a7685a62b8626e577e442db8868718 |
| src/frRegionQuery.h | src/drt/src/frRegionQuery.h | a983aef51fbdccca9c9c43e78142e831b67f35d45b4a81cfa04cfbf5eba6d3c8 |
| src/gc/FlexGC.cpp | src/drt/src/gc/FlexGC.cpp | 45422c35a7d0e7624858a272f985b5c8e3028b4bb79bf202c32678c870ac8779 |
| src/gc/FlexGC.h | src/drt/src/gc/FlexGC.h | c50a8a39d2ba7726b2fa087dee13cc4089fb63d084f51eee48c01861ee14f911 |
| src/gc/FlexGC_cut.cpp | src/drt/src/gc/FlexGC_cut.cpp | d06a6119cfd3bf0869c18654519d7e73638d1e56adb24b634802a06a7c387fb1 |
| src/gc/FlexGC_end.cpp | src/drt/src/gc/FlexGC_end.cpp | a11a2eea8844674cd1cb26a1237bf746c25ac7b16729294008003c6784003831 |
| src/gc/FlexGC_eol.cpp | src/drt/src/gc/FlexGC_eol.cpp | e7a0da0fc41554dba14a3ac04d424fccc40645954b8c2c24bea2447b5a2e015e |
| src/gc/FlexGC_impl.h | src/drt/src/gc/FlexGC_impl.h | fcfff823f66e5e14a0b998024edb54d64d540340433d53ee2eed1d1ace33da8c |
| src/gc/FlexGC_inf.cpp | src/drt/src/gc/FlexGC_inf.cpp | f002a42892b4ba128d70740eed28668c0e0862acca9174100848c0e8bd0469a2 |
| src/gc/FlexGC_init.cpp | src/drt/src/gc/FlexGC_init.cpp | f9e88c0b210fdb18e003fc4acfbf2f261811002d96bd50735eaff6d8b4bfbda9 |
| src/gc/FlexGC_main.cpp | src/drt/src/gc/FlexGC_main.cpp | 2ca3beac37faf6b2d300438906f651f21e66487713393bc8028cd7095eba1b53 |
| src/gc/FlexGC_rq.cpp | src/drt/src/gc/FlexGC_rq.cpp | d2248d5d7a04a38226e4394ff56c10d41e4f8429114c44a0bc906a7c14e6e2e0 |
| src/global.cpp | src/drt/src/global.cpp | 7b559ba15297c203e1ad837242f8ac2456e663dac1b5ef747a7aaef2d9819ef7 |
| src/global.h | src/drt/src/global.h | e5919aa698fdd97c3c06b16d519f446b18be1f5cb09863fd0fed29b01aa9fc5c |
| src/gr/FlexGR.cpp | src/drt/src/gr/FlexGR.cpp | d523d0195a2bcb608c061a4180a076e650062abf1eb0d8440371f94b711068ed |
| src/gr/FlexGR.h | src/drt/src/gr/FlexGR.h | dcff0598c0f74a99337774c203612c337b70ad68f230898d312be54910753410 |
| src/gr/FlexGRCMap.cpp | src/drt/src/gr/FlexGRCMap.cpp | 6befa29208611082961b66f6a1cda0b4cf57a8c80a6c63f97c3c5e3880d8ae1c |
| src/gr/FlexGRCMap.h | src/drt/src/gr/FlexGRCMap.h | b30e05096d20b171e0b78d85cf3df870e44176b44ab5bcf5b4e4252f48912658 |
| src/gr/FlexGRGridGraph.cpp | src/drt/src/gr/FlexGRGridGraph.cpp | f11c4864b197a2b4c53a6ac33e49bdb8dabf612569122371560317f519c1d0d4 |
| src/gr/FlexGRGridGraph.h | src/drt/src/gr/FlexGRGridGraph.h | 760550b62f5a77c2c3ce5619e81bd82782068acb2db79272bb75643d6a110015 |
| src/gr/FlexGRGridGraph_maze.cpp | src/drt/src/gr/FlexGRGridGraph_maze.cpp | 8104ad640908ab2555681b23f6ce3ed5b62e44d6f16547b25cd014a255cad900 |
| src/gr/FlexGRWavefront.h | src/drt/src/gr/FlexGRWavefront.h | 622dc4bd4df7ca6c3eb1a07ef8dda3349b4e9e76d8e6044e64f6973056620a2d |
| src/gr/FlexGR_end.cpp | src/drt/src/gr/FlexGR_end.cpp | ad800c9a75732230ff96bf1cf71bc6d4d40c30d248fa6d89783343454e83d018 |
| src/gr/FlexGR_init.cpp | src/drt/src/gr/FlexGR_init.cpp | 1043a29fc463cc427dcc18dd62bba7a974720fb0057800a78f9c6b5462952569 |
| src/gr/FlexGR_maze.cpp | src/drt/src/gr/FlexGR_maze.cpp | 338216d8a22c22da4cab963bc76f9d11d9a9c81016943aa7c643a7ee00340797 |
| src/gr/FlexGR_rq.cpp | src/drt/src/gr/FlexGR_rq.cpp | b90a21edb7a4d24f5221b1e9dd50fa2e19387b7f1e299db6f6adb3b260184a98 |
| src/gr/FlexGR_topo.cpp | src/drt/src/gr/FlexGR_topo.cpp | b785d1bb28409e0f5fbea3bfbd5a80e06bd3e90296c12f95ba12ccd4a6f7ac5f |
| src/io/io.cpp | src/drt/src/io/io.cpp | 8daae322e4e4c40ba8706e58d9a369d0a1220fcfbaff85e8800d5ee90450130b |
| src/io/io.h | src/drt/src/io/io.h | c659b45235e3bc22691179e4aa8cca784046b9b9a4f180914bfa80ada82c0174 |
| src/io/io_guide.cpp | src/drt/src/io/io_guide.cpp | 5f4c1b1d8a96edf93ab255b4556792e58c3908fff21088adc542ed8970aed834 |
| src/io/io_parser_helper.cpp | src/drt/src/io/io_parser_helper.cpp | 074996ec5b19112e4d83e03036bf3014df182d8c659008eb1779220a7450a92e |
| src/io/io_pin.cpp | src/drt/src/io/io_pin.cpp | 1439cf79eeb69533efe5be901d4abcd6210758c53d2266883929b8a7054a68c2 |
| src/pa/FlexPA.cpp | src/drt/src/pa/FlexPA.cpp | 6b9f61917e487c4b3675e015d2c21af4896b71f3a775f03726366e7c4b510184 |
| src/pa/FlexPA.h | src/drt/src/pa/FlexPA.h | ce9a3ffbaa28cb76e1ecc08eca4462181e2a3285c980f101624f2020ecaf774a |
| src/pa/FlexPA_graphics.cpp | src/drt/src/pa/FlexPA_graphics.cpp | f183b0f32d640cd268287ef36210d0fe6ae5c2734fe4a6488e41a9601396a2c3 |
| src/pa/FlexPA_graphics.h | src/drt/src/pa/FlexPA_graphics.h | d823e3ba7cb5dac1c96d8a5e8cd7e0238870dd327edbf0bfcd32fa2c1f22b2cc |
| src/pa/FlexPA_init.cpp | src/drt/src/pa/FlexPA_init.cpp | dbb8a5a66364bda152cca1d1c2b9ae460c35b7717827bd6fc8d6391a98516e17 |
| src/pa/FlexPA_prep.cpp | src/drt/src/pa/FlexPA_prep.cpp | bec29f11af5769c63af84963a7bb968a7c58aa97f4563b505fbf7976ea1f919e |
| src/pa/FlexPA_unique.cpp | src/drt/src/pa/FlexPA_unique.cpp | a9cfa8a5c57ab55c84a84d4f207c5f339c49007c99dda1548daedd610233825b |
| src/pa/FlexPA_unique.h | src/drt/src/pa/FlexPA_unique.h | dc385bcbec640ea261c653b5d50b7be706adc0dcffe2ae0e1a1295335cdd5ee4 |
| src/rp/FlexRP.cpp | src/drt/src/rp/FlexRP.cpp | 59bcbf0d58fadd4e12af23e378feaac231abaabeb31af7d66daf1b6e84b57d7a |
| src/rp/FlexRP.h | src/drt/src/rp/FlexRP.h | 3bb4ad739080488cc5dbec9dd434e6dfc99c1068d185cf179afd3204e8c78c13 |
| src/rp/FlexRP_init.cpp | src/drt/src/rp/FlexRP_init.cpp | 4d91a6be6d9835c01bbbe16ea41b93df95c235e226995a856fd7b74471f7dfbb |
| src/rp/FlexRP_prep.cpp | src/drt/src/rp/FlexRP_prep.cpp | 02f1176c725685621c14e5bd9afdf2c67f0e050270b5fed970537262c5d9fac2 |
| src/serialization.h | src/drt/src/serialization.h | fcfbdd27596981dce3e73b0c93990864ed7aafb99038fd92e5e78ce3b07106d1 |
| src/ta/FlexTA.cpp | src/drt/src/ta/FlexTA.cpp | 9d523f9fae81365a5bcba519105c78da5e528ffcb6d9d4a3bcfb4735c13eaba6 |
| src/ta/FlexTA.h | src/drt/src/ta/FlexTA.h | 1093148fe5b9a6f0c7f0baa87df056b460c676fbbc729fdf4b23a6ee7baee2bc |
| src/ta/FlexTA_assign.cpp | src/drt/src/ta/FlexTA_assign.cpp | 8ca01cfe3a9ef694886afac1e86547c9389e48c92410774e0706957ac367a669 |
| src/ta/FlexTA_end.cpp | src/drt/src/ta/FlexTA_end.cpp | e64584434f6ce6bb7bd62c1721d93e7b69314fa9d8c28930611c66ce77128185 |
| src/ta/FlexTA_graphics.cpp | src/drt/src/ta/FlexTA_graphics.cpp | 8a53d36d488fef19dd42e0ec53410d08eaf211283d4a2e220c23f6f234c30962 |
| src/ta/FlexTA_graphics.h | src/drt/src/ta/FlexTA_graphics.h | 5f4701b0b032f97e26a94147bad317ade8c44fc287f1282bde1879ef1b633176 |
| src/ta/FlexTA_init.cpp | src/drt/src/ta/FlexTA_init.cpp | cff8d25cf622e59ee48b900b1ca734ded88a321528da89803a465619039f0578 |
| src/ta/FlexTA_rq.cpp | src/drt/src/ta/FlexTA_rq.cpp | 0834bdce3fd929efadb0034f46dbcaaae8464110ff65832d97a617b78c16fd29 |
