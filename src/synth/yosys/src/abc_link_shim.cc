// EOS-owned build glue, not an upstream patch.
//
// Yosys's `abc` pass, with YOSYS_LINK_ABC defined, declares
// `abc::Abc_RealMain` (abc.cc): upstream builds ABC with the
// `abc` namespace when linking it into Yosys. EOS builds ABC
// without a namespace (its frame functions are extern "C" in
// the global namespace, as the AbcEngine facade declares them),
// so the symbol the pass wants does not exist. This shim
// forwards the namespace-qualified declaration to EOS's global
// Abc_RealMain. No upstream file is modified.
extern "C" int Abc_RealMain(int argc, char** argv);

namespace abc {
int Abc_RealMain(int argc, char* argv[]) { return ::Abc_RealMain(argc, argv); }
}  // namespace abc
