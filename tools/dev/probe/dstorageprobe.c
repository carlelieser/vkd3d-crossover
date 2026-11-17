/* DirectStorage probe: drives a game's own dstorage.dll/dstoragecore.dll (placed next to this exe) over
 * vkd3d-proton and checks every request byte-for-byte, with timeouts so hangs show up as failures.
 * Declarations follow Microsoft's dstorage.h (SDK 1.x, MIT licensed), written out for C. */
#define COBJMACROS
#define INITGUID
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- dstorage.h subset ---- */
typedef struct DSTORAGE_QUEUE_DESC { UINT64 SourceType; UINT16 Capacity; INT8 Priority; const char *Name; ID3D12Device *Device; } DSTORAGE_QUEUE_DESC;
/* MSVC layout of DSTORAGE_REQUEST_OPTIONS: byte 0 = CompressionFormat, bytes 1-7 reserved,
 * then a UINT64 with SourceType in bit 0 and DestinationType in bits 1-7. */
typedef struct DSTORAGE_REQUEST_OPTIONS { UINT8 CompressionFormat; UINT8 Reserved1[7]; UINT64 Bits; } DSTORAGE_REQUEST_OPTIONS;
typedef struct IDStorageFile IDStorageFile;
typedef struct { IDStorageFile *Source; UINT64 Offset; UINT32 Size; } DSTORAGE_SOURCE_FILE;
typedef struct { const void *Source; UINT32 Size; } DSTORAGE_SOURCE_MEMORY;
typedef union { DSTORAGE_SOURCE_MEMORY Memory; DSTORAGE_SOURCE_FILE File; } DSTORAGE_SOURCE;
typedef struct { void *Buffer; UINT32 Size; } DSTORAGE_DESTINATION_MEMORY;
typedef struct { ID3D12Resource *Resource; UINT64 Offset; UINT32 Size; } DSTORAGE_DESTINATION_BUFFER;
typedef union { DSTORAGE_DESTINATION_MEMORY Memory; DSTORAGE_DESTINATION_BUFFER Buffer; BYTE pad[40]; } DSTORAGE_DESTINATION;
typedef struct DSTORAGE_REQUEST { DSTORAGE_REQUEST_OPTIONS Options; DSTORAGE_SOURCE Source; DSTORAGE_DESTINATION Destination;
        UINT32 UncompressedSize; UINT64 CancellationTag; const char *Name; } DSTORAGE_REQUEST;
typedef struct { UINT32 FailureCount; HRESULT HResult; UINT32 CommandType; BYTE rest[1024 - 12]; } DSTORAGE_ERROR_RECORD; /* real size 704; padded */

#define SRC_FILE 0
#define SRC_MEMORY 1
#define DST_MEMORY 0
#define DST_BUFFER 1
#define FMT_NONE 0
#define FMT_GDEFLATE 1

typedef struct IDStorageFileVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDStorageFile *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDStorageFile *);
    ULONG (STDMETHODCALLTYPE *Release)(IDStorageFile *);
    void (STDMETHODCALLTYPE *Close)(IDStorageFile *);
    HRESULT (STDMETHODCALLTYPE *GetFileInformation)(IDStorageFile *, BY_HANDLE_FILE_INFORMATION *);
} IDStorageFileVtbl;
struct IDStorageFile { const IDStorageFileVtbl *lpVtbl; };

typedef struct IDStorageFactory IDStorageFactory;
typedef struct IDStorageFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDStorageFactory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDStorageFactory *);
    ULONG (STDMETHODCALLTYPE *Release)(IDStorageFactory *);
    HRESULT (STDMETHODCALLTYPE *CreateQueue)(IDStorageFactory *, const DSTORAGE_QUEUE_DESC *, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *OpenFile)(IDStorageFactory *, const WCHAR *, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *CreateStatusArray)(IDStorageFactory *, UINT32, const char *, REFIID, void **);
    void (STDMETHODCALLTYPE *SetDebugFlags)(IDStorageFactory *, UINT32);
    HRESULT (STDMETHODCALLTYPE *SetStagingBufferSize)(IDStorageFactory *, UINT32);
} IDStorageFactoryVtbl;
struct IDStorageFactory { const IDStorageFactoryVtbl *lpVtbl; };

typedef struct IDStorageStatusArray IDStorageStatusArray;
typedef struct IDStorageStatusArrayVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDStorageStatusArray *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDStorageStatusArray *);
    ULONG (STDMETHODCALLTYPE *Release)(IDStorageStatusArray *);
    BOOLEAN (STDMETHODCALLTYPE *IsComplete)(IDStorageStatusArray *, UINT32);
    HRESULT (STDMETHODCALLTYPE *GetHResult)(IDStorageStatusArray *, UINT32);
} IDStorageStatusArrayVtbl;
struct IDStorageStatusArray { const IDStorageStatusArrayVtbl *lpVtbl; };

typedef struct IDStorageQueue IDStorageQueue;
typedef struct IDStorageQueueVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDStorageQueue *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDStorageQueue *);
    ULONG (STDMETHODCALLTYPE *Release)(IDStorageQueue *);
    void (STDMETHODCALLTYPE *EnqueueRequest)(IDStorageQueue *, const DSTORAGE_REQUEST *);
    void (STDMETHODCALLTYPE *EnqueueStatus)(IDStorageQueue *, IDStorageStatusArray *, UINT32);
    void (STDMETHODCALLTYPE *EnqueueSignal)(IDStorageQueue *, ID3D12Fence *, UINT64);
    void (STDMETHODCALLTYPE *Submit)(IDStorageQueue *);
    void (STDMETHODCALLTYPE *CancelRequestsWithTag)(IDStorageQueue *, UINT64, UINT64);
    void (STDMETHODCALLTYPE *Close)(IDStorageQueue *);
    HANDLE (STDMETHODCALLTYPE *GetErrorEvent)(IDStorageQueue *);
    void (STDMETHODCALLTYPE *RetrieveErrorRecord)(IDStorageQueue *, DSTORAGE_ERROR_RECORD *);
    void (STDMETHODCALLTYPE *Query)(IDStorageQueue *, void *);
    void (STDMETHODCALLTYPE *EnqueueSetEvent)(IDStorageQueue *, HANDLE);
    UINT32 (STDMETHODCALLTYPE *GetCompressionSupport)(IDStorageQueue *, UINT8);
} IDStorageQueueVtbl;
struct IDStorageQueue { const IDStorageQueueVtbl *lpVtbl; };

typedef struct IDStorageCompressionCodec IDStorageCompressionCodec;
typedef struct IDStorageCompressionCodecVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDStorageCompressionCodec *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDStorageCompressionCodec *);
    ULONG (STDMETHODCALLTYPE *Release)(IDStorageCompressionCodec *);
    HRESULT (STDMETHODCALLTYPE *CompressBuffer)(IDStorageCompressionCodec *, const void *, size_t, INT32, void *, size_t, size_t *);
    HRESULT (STDMETHODCALLTYPE *DecompressBuffer)(IDStorageCompressionCodec *, const void *, size_t, void *, size_t, size_t *);
    size_t (STDMETHODCALLTYPE *CompressBufferBound)(IDStorageCompressionCodec *, size_t);
} IDStorageCompressionCodecVtbl;
struct IDStorageCompressionCodec { const IDStorageCompressionCodecVtbl *lpVtbl; };

DEFINE_GUID(IID_IDStorageFactory, 0x6924ea0c, 0xc3cd, 0x4826, 0xb1, 0x0a, 0xf6, 0x4f, 0x4e, 0xd9, 0x27, 0xc1);
DEFINE_GUID(IID_IDStorageFile, 0x5de95e7b, 0x955a, 0x4868, 0xa7, 0x3c, 0x24, 0x3b, 0x29, 0xf4, 0xb8, 0xda);
DEFINE_GUID(IID_IDStorageStatusArray, 0x82397587, 0x7cd5, 0x453b, 0xa0, 0x2e, 0x31, 0x37, 0x9b, 0xd6, 0x46, 0x56);
DEFINE_GUID(IID_IDStorageQueue2, 0xb1c9d643, 0x3a49, 0x44a2, 0xb4, 0x6f, 0x65, 0x36, 0x49, 0x47, 0x0d, 0x18);
DEFINE_GUID(IID_IDStorageCompressionCodec, 0x84ef5121, 0x9b43, 0x4d03, 0xb5, 0xc1, 0xcc, 0x34, 0x60, 0x6b, 0x26, 0x2d);

typedef HRESULT (WINAPI *PFN_GetFactory)(REFIID, void **);
typedef HRESULT (WINAPI *PFN_CreateCodec)(UINT8, UINT32, REFIID, void **);

/* ---- probe ---- */
static ID3D12Device *dev;
static ID3D12CommandQueue *cq;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static IDStorageFactory *factory;
static int fails;

#define DATA_SIZE (8u << 20)
static BYTE *data;

static void result(const char *what, int ok, const char *detail)
{
    if (!ok) fails++;
    printf("%-58s %s %s\n", what, ok ? "OK" : "FAIL <<<", detail ? detail : "");
    fflush(stdout);
}

static ID3D12Resource *make_buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES hp = { type };
    D3D12_RESOURCE_DESC rd = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
            D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
    ID3D12Resource *r = NULL;
    ID3D12Device_CreateCommittedResource(dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)&r);
    return r;
}

static void gpu_wait(void)
{
    ID3D12Fence *f = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&f);
    ID3D12CommandQueue_Signal(cq, f, 1);
    ID3D12Fence_SetEventOnCompletion(f, 1, ev);
    WaitForSingleObject(ev, 10000);
    CloseHandle(ev);
    ID3D12Fence_Release(f);
}

/* Copies a default-heap buffer to a readback buffer and returns a malloc'd copy. */
static BYTE *read_buffer(ID3D12Resource *buf, UINT64 size)
{
    ID3D12Resource *rb = make_buffer(D3D12_HEAP_TYPE_READBACK, size, D3D12_RESOURCE_STATE_COPY_DEST);
    BYTE *out = malloc(size), *p;
    ID3D12CommandAllocator_Reset(alloc);
    ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, rb, 0, buf, 0, size);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12CommandQueue_ExecuteCommandLists(cq, 1, (ID3D12CommandList **)&list);
    gpu_wait();
    ID3D12Resource_Map(rb, 0, NULL, (void **)&p);
    memcpy(out, p, size);
    ID3D12Resource_Unmap(rb, 0, NULL);
    ID3D12Resource_Release(rb);
    return out;
}

static IDStorageQueue *make_queue(UINT64 source_type, const char *name)
{
    DSTORAGE_QUEUE_DESC qd = { source_type, 0x2000, 0, name, dev };
    IDStorageQueue *q = NULL;
    HRESULT hr = factory->lpVtbl->CreateQueue(factory, &qd, &IID_IDStorageQueue2, (void **)&q);
    if (FAILED(hr)) { char d[64]; snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result(name, 0, d); }
    return q;
}

/* Submits what is enqueued, waits on both a D3D12 fence and an event, then checks the error record. */
static int submit_and_wait(IDStorageQueue *q, const char *what, double *ms)
{
    ID3D12Fence *f = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL), fev = CreateEventA(NULL, FALSE, FALSE, NULL);
    DSTORAGE_ERROR_RECORD rec;
    LARGE_INTEGER t0, t1, fr;
    DWORD w1, w2;
    char d[160];

    ID3D12Device_CreateFence(dev, 0, 0, &IID_ID3D12Fence, (void **)&f);
    ID3D12Fence_SetEventOnCompletion(f, 1, fev);
    q->lpVtbl->EnqueueSignal(q, f, 1);
    q->lpVtbl->EnqueueSetEvent(q, ev);
    QueryPerformanceFrequency(&fr); QueryPerformanceCounter(&t0);
    q->lpVtbl->Submit(q);
    w1 = WaitForSingleObject(ev, 20000);
    w2 = WaitForSingleObject(fev, 20000);
    QueryPerformanceCounter(&t1);
    *ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / fr.QuadPart;
    memset(&rec, 0, sizeof(rec));
    q->lpVtbl->RetrieveErrorRecord(q, &rec);
    ID3D12Fence_Release(f); CloseHandle(ev); CloseHandle(fev);
    if (w1 != WAIT_OBJECT_0 || w2 != WAIT_OBJECT_0 || rec.FailureCount)
    {
        snprintf(d, sizeof(d), "(event %s, fence %s, failures %u, first hr 0x%08lx cmd %u)",
                w1 == WAIT_OBJECT_0 ? "ok" : "TIMEOUT", w2 == WAIT_OBJECT_0 ? "ok" : "TIMEOUT",
                rec.FailureCount, (unsigned long)rec.HResult, rec.CommandType);
        result(what, 0, d);
        return 0;
    }
    return 1;
}

static void check(const char *what, const BYTE *got, const BYTE *want, size_t size, double ms)
{
    size_t i;
    char d[128];
    for (i = 0; i < size && got[i] == want[i]; i++);
    if (i == size) snprintf(d, sizeof(d), "(%zu KiB in %.1f ms)", size >> 10, ms);
    else snprintf(d, sizeof(d), "(first mismatch at byte %zu: got %02x want %02x)", i, got[i], want[i]);
    result(what, i == size, d);
}

static void set_req(DSTORAGE_REQUEST *r, UINT8 fmt, UINT64 src, UINT64 dst)
{
    memset(r, 0, sizeof(*r));
    r->Options.CompressionFormat = fmt;
    r->Options.Bits = (src & 1) | (dst << 1);
}

static BOOL write_file(const WCHAR *path, const void *p, DWORD size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    WriteFile(h, p, size, &wr, NULL);
    CloseHandle(h);
    return wr == size;
}

int main(void)
{
    static const WCHAR raw_path[] = L"C:\\dstest\\raw.bin", gd_path[] = L"C:\\dstest\\gdeflate.bin";
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    IDStorageCompressionCodec *codec = NULL;
    IDStorageQueue *fq = NULL, *mq = NULL;
    IDStorageFile *raw = NULL, *gd = NULL;
    PFN_GetFactory get_factory;
    PFN_CreateCodec create_codec;
    DSTORAGE_REQUEST req;
    BYTE *comp = NULL, *mem, *got;
    size_t comp_size = 0, bound;
    ID3D12Resource *buf;
    HMODULE ds;
    HRESULT hr;
    double ms;
    UINT32 i, sup;
    char d[128];

    data = malloc(DATA_SIZE);
    /* Half random-ish, half repetitive so GDeflate has something to do. */
    for (i = 0; i < DATA_SIZE; i++) data[i] = (i < DATA_SIZE / 2) ? (BYTE)((i * 2654435761u) >> 24) : (BYTE)(i / 1024 + (i & 7));

    if (!(ds = LoadLibraryA("dstorage.dll"))) { printf("dstorage.dll not found next to the probe (err %lu)\n", GetLastError()); return 1; }
    get_factory = (PFN_GetFactory)GetProcAddress(ds, "DStorageGetFactory");
    create_codec = (PFN_CreateCodec)GetProcAddress(ds, "DStorageCreateCompressionCodec");

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev);
    printf("D3D12CreateDevice(FL 12_0): hr 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr))
    {
        hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
        snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result("D3D12CreateDevice (FL 11_0)", SUCCEEDED(hr), d);
        if (FAILED(hr)) return 1;
    }
    {
        static const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
                D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2 };
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { 5, fls, 0 };
        D3D12_FEATURE_DATA_D3D12_OPTIONS o = {0};
        ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
        ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o));
        printf("max FL 0x%x, tiled tier %u, binding tier %u, typed UAV load extra %u, logic op %u\n",
                fl.MaxSupportedFeatureLevel, o.TiledResourcesTier, o.ResourceBindingTier,
                o.TypedUAVLoadAdditionalFormats, o.OutputMergerLogicOp);
    }
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&cq);
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);

    hr = get_factory(&IID_IDStorageFactory, (void **)&factory);
    snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result("DStorageGetFactory", SUCCEEDED(hr), d);
    if (FAILED(hr)) return 1;

    /* Test files. */
    CreateDirectoryW(L"C:\\dstest", NULL);
    result("write raw test file (8 MiB)", write_file(raw_path, data, DATA_SIZE), NULL);
    hr = create_codec(FMT_GDEFLATE, 0, &IID_IDStorageCompressionCodec, (void **)&codec);
    snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result("DStorageCreateCompressionCodec(GDeflate)", SUCCEEDED(hr), d);
    if (codec)
    {
        bound = codec->lpVtbl->CompressBufferBound(codec, DATA_SIZE);
        comp = malloc(bound);
        hr = codec->lpVtbl->CompressBuffer(codec, data, DATA_SIZE, 0, comp, bound, &comp_size);
        snprintf(d, sizeof(d), "hr 0x%08lx, %zu -> %zu bytes", (unsigned long)hr, (size_t)DATA_SIZE, comp_size);
        result("CPU GDeflate compress", SUCCEEDED(hr) && comp_size, d);
        result("write GDeflate test file", write_file(gd_path, comp, (DWORD)comp_size), NULL);
    }

    fq = make_queue(SRC_FILE, "probe file queue");
    mq = make_queue(SRC_MEMORY, "probe memory queue");
    if (!fq || !mq) return 1;
    sup = fq->lpVtbl->GetCompressionSupport(fq, FMT_GDEFLATE);
    printf("GetCompressionSupport(GDeflate) = 0x%x (1 GpuOptimized, 2 GpuFallback, 4 CpuFallback, 8 UsingCustomDecompressor?, 0x10 IoRing?)\n", sup);

    hr = factory->lpVtbl->OpenFile(factory, raw_path, &IID_IDStorageFile, (void **)&raw);
    snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result("OpenFile raw", SUCCEEDED(hr), d);
    hr = factory->lpVtbl->OpenFile(factory, gd_path, &IID_IDStorageFile, (void **)&gd);
    snprintf(d, sizeof(d), "hr 0x%08lx", (unsigned long)hr); result("OpenFile gdeflate", SUCCEEDED(hr), d);

    /* 1. file -> memory */
    if (raw)
    {
        mem = calloc(1, DATA_SIZE);
        set_req(&req, FMT_NONE, SRC_FILE, DST_MEMORY);
        req.Source.File.Source = raw; req.Source.File.Size = DATA_SIZE;
        req.Destination.Memory.Buffer = mem; req.Destination.Memory.Size = DATA_SIZE;
        req.UncompressedSize = DATA_SIZE;
        fq->lpVtbl->EnqueueRequest(fq, &req);
        if (submit_and_wait(fq, "file -> memory", &ms)) check("file -> memory", mem, data, DATA_SIZE, ms);
        free(mem);

        /* 2. file -> GPU buffer, at a non-zero offset */
        buf = make_buffer(D3D12_HEAP_TYPE_DEFAULT, DATA_SIZE + 65536, D3D12_RESOURCE_STATE_COMMON);
        set_req(&req, FMT_NONE, SRC_FILE, DST_BUFFER);
        req.Source.File.Source = raw; req.Source.File.Size = DATA_SIZE;
        req.Destination.Buffer.Resource = buf; req.Destination.Buffer.Offset = 65536; req.Destination.Buffer.Size = DATA_SIZE;
        req.UncompressedSize = DATA_SIZE;
        fq->lpVtbl->EnqueueRequest(fq, &req);
        if (submit_and_wait(fq, "file -> GPU buffer", &ms))
        {
            got = read_buffer(buf, DATA_SIZE + 65536);
            check("file -> GPU buffer (offset 64 KiB)", got + 65536, data, DATA_SIZE, ms);
            free(got);
        }
        ID3D12Resource_Release(buf);

        /* 3. many small requests (512 x 16 KiB scattered) -> one buffer */
        buf = make_buffer(D3D12_HEAP_TYPE_DEFAULT, DATA_SIZE, D3D12_RESOURCE_STATE_COMMON);
        for (i = 0; i < 512; i++)
        {
            UINT32 off = ((i * 37) % 512) * 16384;
            set_req(&req, FMT_NONE, SRC_FILE, DST_BUFFER);
            req.Source.File.Source = raw; req.Source.File.Offset = off; req.Source.File.Size = 16384;
            req.Destination.Buffer.Resource = buf; req.Destination.Buffer.Offset = off; req.Destination.Buffer.Size = 16384;
            req.UncompressedSize = 16384;
            fq->lpVtbl->EnqueueRequest(fq, &req);
        }
        if (submit_and_wait(fq, "512 scattered file -> buffer requests", &ms))
        {
            got = read_buffer(buf, DATA_SIZE);
            check("512 scattered file -> buffer requests", got, data, DATA_SIZE, ms);
            free(got);
        }
        ID3D12Resource_Release(buf);
    }

    /* 4. GDeflate file -> GPU buffer (GPU decompression: meta command or DirectStorage's own shader) */
    if (gd && comp_size)
    {
        buf = make_buffer(D3D12_HEAP_TYPE_DEFAULT, DATA_SIZE, D3D12_RESOURCE_STATE_COMMON);
        set_req(&req, FMT_GDEFLATE, SRC_FILE, DST_BUFFER);
        req.Source.File.Source = gd; req.Source.File.Size = (UINT32)comp_size;
        req.Destination.Buffer.Resource = buf; req.Destination.Buffer.Size = DATA_SIZE;
        req.UncompressedSize = DATA_SIZE;
        fq->lpVtbl->EnqueueRequest(fq, &req);
        if (submit_and_wait(fq, "GDeflate file -> GPU buffer", &ms))
        {
            got = read_buffer(buf, DATA_SIZE);
            check("GDeflate file -> GPU buffer (GPU decompression)", got, data, DATA_SIZE, ms);
            free(got);
        }
        ID3D12Resource_Release(buf);

        /* 5. GDeflate file -> memory (CPU decompression) */
        mem = calloc(1, DATA_SIZE);
        set_req(&req, FMT_GDEFLATE, SRC_FILE, DST_MEMORY);
        req.Source.File.Source = gd; req.Source.File.Size = (UINT32)comp_size;
        req.Destination.Memory.Buffer = mem; req.Destination.Memory.Size = DATA_SIZE;
        req.UncompressedSize = DATA_SIZE;
        fq->lpVtbl->EnqueueRequest(fq, &req);
        if (submit_and_wait(fq, "GDeflate file -> memory", &ms)) check("GDeflate file -> memory (CPU decompression)", mem, data, DATA_SIZE, ms);
        free(mem);

        /* 6. GDeflate memory -> GPU buffer */
        buf = make_buffer(D3D12_HEAP_TYPE_DEFAULT, DATA_SIZE, D3D12_RESOURCE_STATE_COMMON);
        set_req(&req, FMT_GDEFLATE, SRC_MEMORY, DST_BUFFER);
        req.Source.Memory.Source = comp; req.Source.Memory.Size = (UINT32)comp_size;
        req.Destination.Buffer.Resource = buf; req.Destination.Buffer.Size = DATA_SIZE;
        req.UncompressedSize = DATA_SIZE;
        mq->lpVtbl->EnqueueRequest(mq, &req);
        if (submit_and_wait(mq, "GDeflate memory -> GPU buffer", &ms))
        {
            got = read_buffer(buf, DATA_SIZE);
            check("GDeflate memory -> GPU buffer", got, data, DATA_SIZE, ms);
            free(got);
        }
        ID3D12Resource_Release(buf);
    }

    if (raw) raw->lpVtbl->Release(raw);
    if (gd) gd->lpVtbl->Release(gd);
    fq->lpVtbl->Release(fq);
    mq->lpVtbl->Release(mq);
    factory->lpVtbl->Release(factory);
    printf("done, %d failures\n", fails);
    return 0;
}
