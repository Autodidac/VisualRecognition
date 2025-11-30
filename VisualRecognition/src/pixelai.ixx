
    export module pixelai;

    import std;

    export namespace pixelai
    {
        using std::optional;
        using std::string;
        using std::vector;
        using std::span;

        struct Patch
        {
            int                 width{};
            int                 height{};
            vector<std::uint32_t> pixels;  // BGRA32
            string              label;
        };

        // Extremely simple nearest-neighbor patch classifier over BGRA32.
        //
        // This is intentionally minimal and deterministic; it is not meant to
        // be a deep model, just a compact incremental learner that can be
        // serialized to disk.
        export class PixelRecognizer
        {
        public:
            PixelRecognizer() = default;

            [[nodiscard]] bool add_example_bgra32(
                span<const std::uint32_t> pixels,
                int width,
                int height,
                const string& label)
            {
                if (width <= 0 || height <= 0 || pixels.size() !=
                    static_cast<std::size_t>(width * height))
                {
                    return false;
                }

                Patch p;
                p.width  = width;
                p.height = height;
                p.label  = label;
                p.pixels.assign(pixels.begin(), pixels.end());
                examples_.push_back(std::move(p));
                return true;
            }

            [[nodiscard]] optional<string> classify_bgra32(
                span<const std::uint32_t> pixels,
                int width,
                int height,
                float* out_score = nullptr) const
            {
                if (examples_.empty())
                    return std::nullopt;

                if (width <= 0 || height <= 0 ||
                    pixels.size() != static_cast<std::size_t>(width * height))
                {
                    return std::nullopt;
                }

                const auto downsample = [](span<const std::uint32_t> src,
                                           int w, int h,
                                           int targetW,
                                           int targetH) -> vector<float>
                {
                    vector<float> out;
                    out.resize(static_cast<std::size_t>(targetW * targetH) * 3);

                    for (int ty = 0; ty < targetH; ++ty)
                    {
                        const float sy = (ty + 0.5f) * (static_cast<float>(h) / targetH);
                        const int   syi = std::clamp(static_cast<int>(sy), 0, h - 1);

                        for (int tx = 0; tx < targetW; ++tx)
                        {
                            const float sx = (tx + 0.5f) * (static_cast<float>(w) / targetW);
                            const int   sxi = std::clamp(static_cast<int>(sx), 0, w - 1);

                            const std::uint32_t px = src[static_cast<std::size_t>(syi * w + sxi)];

                            const std::uint8_t b = static_cast<std::uint8_t>( px        & 0xFFu);
                            const std::uint8_t g = static_cast<std::uint8_t>((px >> 8)  & 0xFFu);
                            const std::uint8_t r = static_cast<std::uint8_t>((px >> 16) & 0xFFu);

                            const std::size_t dstIndex =
                                static_cast<std::size_t>(ty * targetW + tx) * 3u;

                            out[dstIndex + 0] = static_cast<float>(r) / 255.0f;
                            out[dstIndex + 1] = static_cast<float>(g) / 255.0f;
                            out[dstIndex + 2] = static_cast<float>(b) / 255.0f;
                        }
                    }

                    return out;
                };

                constexpr int kFeatW = 16;
                constexpr int kFeatH = 16;

                const auto queryFeat = downsample(pixels, width, height, kFeatW, kFeatH);

                auto distance_sq = [](span<const float> a, span<const float> b) -> float
                {
                    const std::size_t n = a.size();
                    float acc = 0.0f;
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        const float d = a[i] - b[i];
                        acc += d * d;
                    }
                    return acc;
                };

                optional<string> bestLabel;
                float            bestDist = std::numeric_limits<float>::max();

                vector<float> tmp;

                for (const auto& ex : examples_)
                {
                    tmp = downsample(ex.pixels, ex.width, ex.height, kFeatW, kFeatH);
                    const float d = distance_sq(queryFeat, tmp);
                    if (d < bestDist)
                    {
                        bestDist  = d;
                        bestLabel = ex.label;
                    }
                }

                if (!bestLabel)
                    return std::nullopt;

                // Turn distance into a crude confidence in [0,1].
                const float score = 1.0f / (1.0f + std::sqrt(bestDist) * 0.25f);
                if (out_score)
                    *out_score = score;

                return bestLabel;
            }

            [[nodiscard]] bool save_to_file(const string& path) const
            {
                std::ofstream ofs(path, std::ios::binary);
                if (!ofs) return false;

                const std::uint32_t count = static_cast<std::uint32_t>(examples_.size());
                ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

                for (const auto& ex : examples_)
                {
                    const std::uint32_t w  = static_cast<std::uint32_t>(ex.width);
                    const std::uint32_t h  = static_cast<std::uint32_t>(ex.height);
                    const std::uint32_t n  = static_cast<std::uint32_t>(ex.pixels.size());
                    const std::uint32_t ls = static_cast<std::uint32_t>(ex.label.size());

                    ofs.write(reinterpret_cast<const char*>(&w),  sizeof(w));
                    ofs.write(reinterpret_cast<const char*>(&h),  sizeof(h));
                    ofs.write(reinterpret_cast<const char*>(&n),  sizeof(n));
                    ofs.write(reinterpret_cast<const char*>(&ls), sizeof(ls));

                    ofs.write(reinterpret_cast<const char*>(ex.pixels.data()),
                              static_cast<std::streamsize>(n * sizeof(std::uint32_t)));
                    ofs.write(ex.label.data(),
                              static_cast<std::streamsize>(ls));
                }

                return static_cast<bool>(ofs);
            }

            [[nodiscard]] bool load_from_file(const string& path)
            {
                std::ifstream ifs(path, std::ios::binary);
                if (!ifs) return false;

                vector<Patch> tmp;

                std::uint32_t count{};
                ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
                if (!ifs) return false;

                for (std::uint32_t i = 0; i < count; ++i)
                {
                    std::uint32_t w{}, h{}, n{}, ls{};
                    ifs.read(reinterpret_cast<char*>(&w),  sizeof(w));
                    ifs.read(reinterpret_cast<char*>(&h),  sizeof(h));
                    ifs.read(reinterpret_cast<char*>(&n),  sizeof(n));
                    ifs.read(reinterpret_cast<char*>(&ls), sizeof(ls));
                    if (!ifs) return false;

                    Patch ex;
                    ex.width  = static_cast<int>(w);
                    ex.height = static_cast<int>(h);
                    ex.pixels.resize(n);
                    ex.label.resize(ls);

                    ifs.read(reinterpret_cast<char*>(ex.pixels.data()),
                             static_cast<std::streamsize>(n * sizeof(std::uint32_t)));
                    ifs.read(ex.label.data(),
                             static_cast<std::streamsize>(ls));

                    if (!ifs) return false;

                    tmp.push_back(std::move(ex));
                }

                examples_ = std::move(tmp);
                return true;
            }

            [[nodiscard]] bool empty() const noexcept
            {
                return examples_.empty();
            }

            [[nodiscard]] std::size_t size() const noexcept
            {
                return examples_.size();
            }

        private:
            vector<Patch> examples_;
        };

    } // namespace almond::pixelai
