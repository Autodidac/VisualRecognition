export module pixelai;

import std;

export namespace almond::pixelai
{
    using std::size_t;
    using std::span;
    using std::string;
    using std::vector;
    using std::optional;

    struct Patch
    {
        int           width{};
        int           height{};
        vector<float> features; // normalized RGB triplets
    };

    struct LabeledPatch
    {
        string label;
        Patch  patch;
    };

    export class PixelRecognizer
    {
    public:
        PixelRecognizer() = default;

        void set_min_confidence(float v) noexcept { min_confidence_ = v; }
        [[nodiscard]] float min_confidence() const noexcept { return min_confidence_; }

        void clear() noexcept
        {
            examples_.clear();
            patch_width_  = 0;
            patch_height_ = 0;
        }

        // pixels are 0xAARRGGBB or 0x00RRGGBB (we only use RGB)
        [[nodiscard]] bool add_example_bgra32(span<const std::uint32_t> pixels,
                                              int                        width,
                                              int                        height,
                                              std::string                label)
        {
            if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height))
                return false;

            // Enforce consistent patch size for simplicity
            if (!examples_.empty())
            {
                if (width != patch_width_ || height != patch_height_)
                    return false;
            }
            else
            {
                patch_width_  = width;
                patch_height_ = height;
            }

            Patch p = make_patch_from_bgra32(pixels, width, height);
            if (p.features.empty())
                return false;

            examples_.push_back(LabeledPatch{
                .label = std::move(label),
                .patch = std::move(p)
            });
            return true;
        }

        [[nodiscard]] optional<string> classify_bgra32(span<const std::uint32_t> pixels,
            int                        width,
            int                        height,
            float* out_score = nullptr) const
        {
            if (examples_.empty())
                return std::nullopt;

            if (width != patch_width_ || height != patch_height_)
                return std::nullopt;

            Patch q = make_patch_from_bgra32(pixels, width, height);
            if (q.features.empty())
                return std::nullopt;

            float best_score = -1.0f;
            auto  best_example = static_cast<LabeledPatch const*>(nullptr);

            std::span<const float> qv{ q.features.data(), q.features.size() };

            for (auto const& ex : examples_)
            {
                std::span<const float> ev{ ex.patch.features.data(),
                                          ex.patch.features.size() };
                if (ev.size() != qv.size())
                    continue;

                float s = cosine(ev, qv);
                if (s > best_score)
                {
                    best_score = s;
                    best_example = &ex;
                }
            }

            if (!best_example || best_score < min_confidence_)
                return std::nullopt;

            if (out_score)
                *out_score = best_score;

            return best_example->label;
        }

        [[nodiscard]] bool save_to_file(const std::string& path) const
        {
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs)
                return false;

            const char magic[5] = {'P','X','A','I','1'};
            ofs.write(magic, sizeof(magic));
            if (!ofs)
                return false;

            std::uint32_t count = static_cast<std::uint32_t>(examples_.size());
            ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
            if (!ofs)
                return false;

            ofs.write(reinterpret_cast<const char*>(&patch_width_), sizeof(patch_width_));
            ofs.write(reinterpret_cast<const char*>(&patch_height_), sizeof(patch_height_));
            if (!ofs)
                return false;

            for (auto const& ex : examples_)
            {
                std::uint32_t label_len = static_cast<std::uint32_t>(ex.label.size());
                ofs.write(reinterpret_cast<const char*>(&label_len), sizeof(label_len));
                ofs.write(ex.label.data(), static_cast<std::streamsize>(label_len));

                std::uint32_t feat_count = static_cast<std::uint32_t>(ex.patch.features.size());
                ofs.write(reinterpret_cast<const char*>(&feat_count), sizeof(feat_count));
                ofs.write(reinterpret_cast<const char*>(ex.patch.features.data()),
                          static_cast<std::streamsize>(feat_count * sizeof(float)));

                if (!ofs)
                    return false;
            }

            return true;
        }

        [[nodiscard]] bool load_from_file(const std::string& path)
        {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs)
                return false;

            char magic[5]{};
            ifs.read(magic, sizeof(magic));
            if (!ifs || magic[0] != 'P' || magic[1] != 'X' || magic[2] != 'A' || magic[3] != 'I')
                return false;

            std::uint32_t count{};
            ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (!ifs)
                return false;

            ifs.read(reinterpret_cast<char*>(&patch_width_), sizeof(patch_width_));
            ifs.read(reinterpret_cast<char*>(&patch_height_), sizeof(patch_height_));
            if (!ifs)
                return false;

            if (patch_width_ <= 0 || patch_height_ <= 0)
                return false;

            const size_t expected_feat_count = static_cast<size_t>(patch_width_)
                                              * static_cast<size_t>(patch_height_)
                                              * 3u;
            if (expected_feat_count == 0
                || expected_feat_count > static_cast<size_t>(std::numeric_limits<std::uint32_t>::max()))
                return false;

            vector<LabeledPatch> tmp;
            tmp.reserve(count);

            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t label_len{};
                ifs.read(reinterpret_cast<char*>(&label_len), sizeof(label_len));
                if (!ifs)
                    return false;

                string label;
                label.resize(label_len);
                ifs.read(label.data(), static_cast<std::streamsize>(label_len));
                if (!ifs)
                    return false;

                std::uint32_t feat_count{};
                ifs.read(reinterpret_cast<char*>(&feat_count), sizeof(feat_count));
                if (!ifs)
                    return false;

                if (static_cast<size_t>(feat_count) != expected_feat_count)
                    return false;

                Patch p;
                p.width  = patch_width_;
                p.height = patch_height_;
                p.features.resize(expected_feat_count);

                ifs.read(reinterpret_cast<char*>(p.features.data()),
                         static_cast<std::streamsize>(feat_count * sizeof(float)));
                if (!ifs)
                    return false;

                tmp.push_back(LabeledPatch{
                    .label = std::move(label),
                    .patch = std::move(p)
                });
            }

            examples_ = std::move(tmp);
            return true;
        }

    private:
        int   patch_width_{};
        int   patch_height_{};
        float min_confidence_{0.85f};
        vector<LabeledPatch> examples_{};

        [[nodiscard]] static Patch make_patch_from_bgra32(span<const std::uint32_t> pixels,
                                                          int                        width,
                                                          int                        height)
        {
            Patch p;
            p.width  = width;
            p.height = height;
            const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
            p.features.reserve(count * 3);

            constexpr float inv255 = 1.0f / 255.0f;

            for (size_t i = 0; i < count; ++i)
            {
                std::uint32_t v = pixels[i];
                std::uint8_t b = static_cast<std::uint8_t>(v & 0xFFu);
                std::uint8_t g = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
                std::uint8_t r = static_cast<std::uint8_t>((v >> 16) & 0xFFu);

                p.features.push_back(r * inv255);
                p.features.push_back(g * inv255);
                p.features.push_back(b * inv255);
            }

            // L2-normalize
            float sum_sq = 0.0f;
            for (float v : p.features)
                sum_sq += v * v;

            if (sum_sq > 0.0f)
            {
                float inv = 1.0f / std::sqrt(sum_sq);
                for (float& v : p.features)
                    v *= inv;
            }

            return p;
        }

        [[nodiscard]] static float cosine(std::span<const float> a,
            std::span<const float> b) noexcept
        {
            if (a.size() != b.size() || a.empty())
                return -1.0f;

            float dot = 0.0f;
            for (std::size_t i = 0; i < a.size(); ++i)
                dot += a[i] * b[i];
            return dot;
        }
    };
}
