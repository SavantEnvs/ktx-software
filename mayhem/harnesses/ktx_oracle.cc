// Functional oracle for mayhem/test.sh: known-answer checks of libktx's reader against the committed
// seed files. Each check compares header fields, the FNV-1a hash of the loaded image data and, for Basis
// Universal textures, of the data transcoded to RGBA32 with golden values. Built with normal flags.
//   ktx_oracle <seed dir>          prints PASS/FAIL lines and a SUMMARY line, exits non-zero on any FAIL
//   ktx_oracle --dump <seed dir>   prints the golden table rows for the files in the directory
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <ktx.h>

namespace {

struct Golden {
    const char* file;
    uint32_t classId, vkFormat, width, height, depth, levels, layers, faces;
    uint64_t dataSize, dataHash, rgbaHash;
};

#include "ktx_oracle_golden.inc"

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    fclose(f);
    return true;
}

int g_pass = 0, g_fail = 0;

void Report(bool ok, const std::string& name, const std::string& detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : ": ", detail.c_str());
    (ok ? g_pass : g_fail)++;
}

struct Facts {
    Golden g{};
    bool loaded = false;
};

// Loads `data` and fills the facts; `transcode` additionally transcodes Basis data to RGBA32.
Facts Load(const std::vector<uint8_t>& data, const char* file) {
    Facts f;
    f.g.file = file;
    ktxTexture* t = nullptr;
    if (ktxTexture_CreateFromMemory(data.data(), data.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &t) != KTX_SUCCESS) return f;
    if (ktxTexture_LoadImageData(t, nullptr, 0) != KTX_SUCCESS) { ktxTexture_Destroy(t); return f; }
    f.loaded = true;
    f.g.classId = t->classId;
    f.g.vkFormat = t->classId == ktxTexture2_c ? ((ktxTexture2*)t)->vkFormat : 0;
    f.g.width = t->baseWidth; f.g.height = t->baseHeight; f.g.depth = t->baseDepth;
    f.g.levels = t->numLevels; f.g.layers = t->numLayers; f.g.faces = t->numFaces;
    f.g.dataSize = ktxTexture_GetDataSize(t);
    f.g.dataHash = Fnv(ktxTexture_GetData(t), f.g.dataSize);
    f.g.rgbaHash = 0;
    if (t->classId == ktxTexture2_c && ktxTexture2_NeedsTranscoding((ktxTexture2*)t)) {
        if (ktxTexture2_TranscodeBasis((ktxTexture2*)t, KTX_TTF_RGBA32, 0) == KTX_SUCCESS)
            f.g.rgbaHash = Fnv(ktxTexture_GetData(t), ktxTexture_GetDataSize(t));
    }
    ktxTexture_Destroy(t);
    return f;
}

void CheckGolden(const std::string& dir, const Golden& g) {
    std::vector<uint8_t> data;
    if (!ReadFile(dir + "/" + g.file, &data)) { Report(false, g.file, "cannot read"); return; }
    Facts f = Load(data, g.file);
    char detail[256] = "";
    bool ok = f.loaded &&
        f.g.classId == g.classId && f.g.vkFormat == g.vkFormat && f.g.width == g.width &&
        f.g.height == g.height && f.g.depth == g.depth && f.g.levels == g.levels &&
        f.g.layers == g.layers && f.g.faces == g.faces && f.g.dataSize == g.dataSize &&
        f.g.dataHash == g.dataHash && f.g.rgbaHash == g.rgbaHash;
    if (!ok)
        snprintf(detail, sizeof detail, "loaded=%d %ux%ux%u levels=%u dataSize=%llu hash=%llx rgba=%llx",
                 f.loaded, f.g.width, f.g.height, f.g.depth, f.g.levels,
                 (unsigned long long)f.g.dataSize, (unsigned long long)f.g.dataHash,
                 (unsigned long long)f.g.rgbaHash);
    Report(ok, std::string("golden ") + g.file, detail);
}

// Malformed input must be rejected with the documented error codes.
void CheckRejections(const std::string& dir) {
    std::vector<uint8_t> data;
    if (!ReadFile(dir + "/r8g8b8a8_srgb_mip.ktx2", &data)) { Report(false, "reject: read seed"); return; }
    ktxTexture* t = nullptr;

    std::vector<uint8_t> bad = data;
    bad[1] = 'X';
    KTX_error_code rc = ktxTexture_CreateFromMemory(bad.data(), bad.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &t);
    Report(rc == KTX_UNKNOWN_FILE_FORMAT && t == nullptr, "reject: bad identifier", ktxErrorString(rc));

    rc = ktxTexture_CreateFromMemory(data.data(), 40, KTX_TEXTURE_CREATE_NO_FLAGS, &t);
    Report(rc == KTX_FILE_DATA_ERROR || rc == KTX_FILE_UNEXPECTED_EOF, "reject: truncated header", ktxErrorString(rc));

    rc = ktxTexture_CreateFromMemory(data.data(), data.size() / 2, KTX_TEXTURE_CREATE_NO_FLAGS, &t);
    bool rejected = rc != KTX_SUCCESS;
    if (rc == KTX_SUCCESS) {
        rejected = ktxTexture_LoadImageData(t, nullptr, 0) != KTX_SUCCESS;
        ktxTexture_Destroy(t);
        t = nullptr;
    }
    Report(rejected, "reject: truncated image data");

    rc = ktxTexture_CreateFromMemory(nullptr, 0, KTX_TEXTURE_CREATE_NO_FLAGS, &t);
    Report(rc == KTX_INVALID_VALUE, "reject: null input", ktxErrorString(rc));
}

// Offsets and sizes the library derives from the header of a known texture.
void CheckLayout(const std::string& dir) {
    std::vector<uint8_t> data;
    if (!ReadFile(dir + "/r8g8b8a8_srgb_mip.ktx2", &data)) { Report(false, "layout: read seed"); return; }
    ktxTexture* t = nullptr;
    if (ktxTexture_CreateFromMemory(data.data(), data.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &t) != KTX_SUCCESS) {
        Report(false, "layout: create"); return;
    }
    size_t off1 = 0, off2 = 0;
    bool ok = ktxTexture_GetImageOffset(t, 0, 0, 0, &off1) == KTX_SUCCESS &&
              ktxTexture_GetImageOffset(t, 1, 0, 0, &off2) == KTX_SUCCESS;
    size_t size0 = ktxTexture_GetImageSize(t, 0), size1 = ktxTexture_GetImageSize(t, 1);
    uint32_t w = t->baseWidth, h = t->baseHeight;
    ok = ok && size0 == (size_t)w * h * 4 && size1 == (size_t)(w / 2 ? w / 2 : 1) * (h / 2 ? h / 2 : 1) * 4 &&
         ktxTexture_GetRowPitch(t, 0) == w * 4 && ktxTexture_GetElementSize(t) == 4 && off1 != off2;
    Report(ok, "layout: image offsets, sizes and row pitch");
    ktxTexture_Destroy(t);
}

}  // namespace

int main(int argc, char** argv) {
    bool dump = argc > 2 && !strcmp(argv[1], "--dump");
    if (argc < 2 || (argc > 2 && !dump)) { fprintf(stderr, "usage: ktx_oracle [--dump] <seed dir>\n"); return 2; }
    std::string dir = argv[dump ? 2 : 1];
    if (dump) {
        for (int i = 3; i < argc; ++i) {
            std::vector<uint8_t> data;
            if (!ReadFile(dir + "/" + argv[i], &data)) continue;
            Facts f = Load(data, argv[i]);
            if (!f.loaded) { printf("// %s: not loadable\n", argv[i]); continue; }
            printf("    {\"%s\", %u, %u, %u, %u, %u, %u, %u, %u, %lluull, 0x%llxull, 0x%llxull},\n", argv[i],
                   f.g.classId, f.g.vkFormat, f.g.width, f.g.height, f.g.depth, f.g.levels, f.g.layers,
                   f.g.faces, (unsigned long long)f.g.dataSize, (unsigned long long)f.g.dataHash,
                   (unsigned long long)f.g.rgbaHash);
        }
        return 0;
    }
    for (const Golden& g : kGolden) CheckGolden(dir, g);
    CheckRejections(dir);
    CheckLayout(dir);
    printf("SUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
