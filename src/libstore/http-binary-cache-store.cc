#include "binary-cache-store.hh"
#include "download.hh"
#include "globals.hh"
#include "nar-info-disk-cache.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

class HttpBinaryCacheStore : public BinaryCacheStore
{
private:

    Path cacheUri;

    Pool<Downloader> downloaders;

public:

    HttpBinaryCacheStore(
        const StoreParams & params, const Path & _cacheUri)
        : BinaryCacheStore(params)
        , cacheUri(_cacheUri)
        , downloaders(
            std::numeric_limits<size_t>::max(),
            []() { return makeDownloader(); })
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        diskCache = getNarInfoDiskCache();
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (!diskCache->cacheExists(cacheUri)) {
            try {
                BinaryCacheStore::init();
            } catch (UploadToHTTP &) {
                throw Error(format("‘%s’ does not appear to be a binary cache") % cacheUri);
            }
            diskCache->createCache(cacheUri, wantMassQuery_, priority);
        }
    }

protected:

    bool fileExists(const std::string & path) override
    {
        try {
            auto downloader(downloaders.get());
            DownloadOptions options;
            options.showProgress = DownloadOptions::no;
            options.head = true;
            downloader->download(cacheUri + "/" + path, options);
            return true;
        } catch (DownloadError & e) {
            /* S3 buckets return 403 if a file doesn't exist and the
               bucket is unlistable, so treat 403 as 404. */
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                return false;
            throw;
        }
    }

    void upsertFile(const std::string & path, const std::string & data) override
    {
        throw UploadToHTTP("uploading to an HTTP binary cache is not supported");
    }

    std::shared_ptr<std::string> getFile(const std::string & path) override
    {
        auto downloader(downloaders.get());
        DownloadOptions options;
        options.showProgress = DownloadOptions::no;
        try {
            return downloader->download(cacheUri + "/" + path, options).data;
        } catch (DownloadError & e) {
            if (e.error == Downloader::NotFound || e.error == Downloader::Forbidden)
                return 0;
            throw;
        }
    }

};

static RegisterStoreImplementation regStore([](
    const std::string & uri, const StoreParams & params)
    -> std::shared_ptr<Store>
{
    if (std::string(uri, 0, 7) != "http://" &&
        std::string(uri, 0, 8) != "https://") return 0;
    auto store = std::make_shared<HttpBinaryCacheStore>(params, uri);
    store->init();
    return store;
});

}

