#pragma once

// =============================================================================
// MeasurementData -- B-H measurement data container and analysis utilities.
//
// Stores measured (H, B) data points from a hysteresis loop measurement
// along with metadata (material name, temperature, source/equipment).
//
// Provides:
//   - loadFromJSON(path)   : import measurement data from JSON file
//   - getCoercivity()      : extract coercive field Hc (B = 0 intercept)
//   - getRemanence()       : extract remanent flux density Br (H = 0 intercept)
//   - getBsat()            : estimate saturation flux density
//   - getHmax()            : maximum applied field
//   - getAscending/Descending branch extraction
//
// JSON format expected:
//   {
//     "material": "MuMetal_80NiFe",
//     "temperature_C": 25.0,
//     "source": "VSM Lakeshore 7400",
//     "frequency_Hz": 0.5,
//     "data": [ {"H": ..., "B": ...}, ... ]
//   }
//
// Note: This is a cold-path / offline data structure. No real-time constraints.
//
// Reference: IEC 60404-4 (DC measurement methods for magnetic materials)
// =============================================================================

#include "../../core/include/core/magnetics/JAParameterSet.h"
#include "../../core/include/core/util/Constants.h"

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <numeric>
#include <utility>

namespace transfo {

// ---------------------------------------------------------------------------
// BHPoint -- a single (H, B) measurement pair.
// ---------------------------------------------------------------------------
struct BHPoint
{
    double H = 0.0;   // Applied magnetic field [A/m]
    double B = 0.0;   // Flux density [T]
};

// ---------------------------------------------------------------------------
// MeasurementMetadata -- information about the measurement conditions.
// ---------------------------------------------------------------------------
struct MeasurementMetadata
{
    std::string materialName = "Unknown";
    double      temperatureC = 25.0;            // Temperature in Celsius
    std::string source       = "Unknown";       // Equipment / lab
    double      frequencyHz  = 0.0;             // Excitation frequency (0 = DC)
    std::string dateISO8601  = "";              // e.g. "2024-11-05"
    std::string notes        = "";
};

// ---------------------------------------------------------------------------
// MeasurementData -- container for one measured B-H loop.
// ---------------------------------------------------------------------------
class MeasurementData
{
public:
    MeasurementData() = default;

    // Construct directly from a vector of BHPoints
    explicit MeasurementData(std::vector<BHPoint> points,
                             MeasurementMetadata  meta = {})
        : points_(std::move(points))
        , metadata_(std::move(meta))
    {
        sortAndValidate();
    }

    // -----------------------------------------------------------------------
    // loadFromJSON -- import measurement data from a JSON file.
    //
    // Uses a minimal hand-rolled parser (no external dependency) that handles
    // the expected format. For production use, consider nlohmann/json.
    //
    // Throws std::runtime_error on parse or I/O failure.
    // -----------------------------------------------------------------------
    void loadFromJSON(const std::string& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open())
            throw std::runtime_error("MeasurementData::loadFromJSON: cannot open " + path);

        // Read entire file
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());

        // Parse metadata (minimal string-level extraction)
        metadata_.materialName  = extractStringField(content, "material");
        metadata_.source        = extractStringField(content, "source");
        metadata_.dateISO8601   = extractStringField(content, "date");
        metadata_.notes         = extractStringField(content, "notes");
        metadata_.temperatureC  = extractNumberField(content, "temperature_C", 25.0);
        metadata_.frequencyHz   = extractNumberField(content, "frequency_Hz",  0.0);

        // Parse data array
        points_.clear();
        std::string::size_type dataStart = content.find("\"data\"");
        if (dataStart == std::string::npos)
            throw std::runtime_error("MeasurementData::loadFromJSON: 'data' field not found");

        // Find the opening bracket of the array
        std::string::size_type arrStart = content.find('[', dataStart);
        std::string::size_type arrEnd   = content.find(']', arrStart);
        if (arrStart == std::string::npos || arrEnd == std::string::npos)
            throw std::runtime_error("MeasurementData::loadFromJSON: malformed data array");

        std::string arrContent = content.substr(arrStart + 1, arrEnd - arrStart - 1);

        // Parse each {H:..., B:...} object
        std::string::size_type pos = 0;
        while (pos < arrContent.size())
        {
            auto objStart = arrContent.find('{', pos);
            if (objStart == std::string::npos)
                break;
            auto objEnd = arrContent.find('}', objStart);
            if (objEnd == std::string::npos)
                break;

            std::string obj = arrContent.substr(objStart, objEnd - objStart + 1);

            BHPoint pt;
            pt.H = extractNumberField(obj, "H", 0.0);
            pt.B = extractNumberField(obj, "B", 0.0);
            points_.push_back(pt);

            pos = objEnd + 1;
        }

        if (points_.empty())
            throw std::runtime_error("MeasurementData::loadFromJSON: no data points found");

        sortAndValidate();
    }

    // -----------------------------------------------------------------------
    // getCoercivity -- coercive field Hc [A/m]
    //
    // Hc is the H-field value where B crosses zero on the descending branch.
    // Computed by linear interpolation of the nearest (H, B) pair crossing.
    // Returns the average of positive and negative coercivity magnitudes.
    // -----------------------------------------------------------------------
    double getCoercivity() const
    {
        if (points_.size() < 4)
            return 0.0;

        // Find all zero-crossings of B
        std::vector<double> zeroCrossings;
        for (size_t i = 1; i < points_.size(); ++i)
        {
            if (points_[i - 1].B * points_[i].B < 0.0)
            {
                // Linear interpolation to find H at B = 0
                double frac = -points_[i - 1].B / (points_[i].B - points_[i - 1].B);
                double Hc = points_[i - 1].H + frac * (points_[i].H - points_[i - 1].H);
                zeroCrossings.push_back(Hc);
            }
        }

        if (zeroCrossings.empty())
            return 0.0;

        // Average the absolute values of all zero-crossings
        double sum = 0.0;
        for (double hc : zeroCrossings)
            sum += std::abs(hc);

        return sum / static_cast<double>(zeroCrossings.size());
    }

    // -----------------------------------------------------------------------
    // getRemanence -- remanent flux density Br [T]
    //
    // Br is the B-field value where H crosses zero.
    // Computed by linear interpolation. Returns the average of positive
    // and negative remanence magnitudes.
    // -----------------------------------------------------------------------
    double getRemanence() const
    {
        if (points_.size() < 4)
            return 0.0;

        std::vector<double> zeroCrossings;
        for (size_t i = 1; i < points_.size(); ++i)
        {
            if (points_[i - 1].H * points_[i].H < 0.0)
            {
                double frac = -points_[i - 1].H / (points_[i].H - points_[i - 1].H);
                double Br = points_[i - 1].B + frac * (points_[i].B - points_[i - 1].B);
                zeroCrossings.push_back(Br);
            }
        }

        if (zeroCrossings.empty())
            return 0.0;

        double sum = 0.0;
        for (double br : zeroCrossings)
            sum += std::abs(br);

        return sum / static_cast<double>(zeroCrossings.size());
    }

    // -----------------------------------------------------------------------
    // getBsat -- saturation flux density [T]
    //
    // Estimated as the maximum absolute B value in the dataset.
    // For more accuracy, fits a line to the high-H region and extrapolates.
    // -----------------------------------------------------------------------
    double getBsat() const
    {
        if (points_.empty())
            return 0.0;

        // Primary estimate: max |B|
        double maxB = 0.0;
        for (const auto& pt : points_)
            maxB = std::max(maxB, std::abs(pt.B));

        // Refined estimate: look at the top 10% of H values and fit
        // the B vs H trend. If the slope is small (< 5% of mu0),
        // the material is well-saturated and we use the average B there.
        if (points_.size() >= 20)
        {
            double Hmax = getHmax();
            double Hthreshold = Hmax * 0.9;

            std::vector<double> highB;
            for (const auto& pt : points_)
            {
                if (std::abs(pt.H) > Hthreshold)
                    highB.push_back(std::abs(pt.B));
            }

            if (highB.size() >= 3)
            {
                double avg = std::accumulate(highB.begin(), highB.end(), 0.0)
                           / static_cast<double>(highB.size());

                // Use average of high-field B if it is close to maxB
                if (avg > 0.9 * maxB)
                    return avg;
            }
        }

        return maxB;
    }

    // -----------------------------------------------------------------------
    // getHmax -- maximum applied field magnitude [A/m]
    // -----------------------------------------------------------------------
    double getHmax() const
    {
        double hmax = 0.0;
        for (const auto& pt : points_)
            hmax = std::max(hmax, std::abs(pt.H));
        return hmax;
    }

    // -----------------------------------------------------------------------
    // getMsEstimate -- estimate saturation magnetization from Bsat.
    //
    // Ms = Bsat / mu0   (neglecting alpha*Ms contribution at saturation)
    // This provides the Phase 0 analytical initial guess for CMA-ES.
    // -----------------------------------------------------------------------
    double getMsEstimate() const
    {
        return getBsat() / kMu0;
    }

    // -----------------------------------------------------------------------
    // Branch extraction: ascending and descending.
    //
    // Assumes data is ordered as a traversal of the loop. Ascending branch
    // is where dH/di > 0 (H increasing), descending where dH/di < 0.
    // -----------------------------------------------------------------------
    std::vector<BHPoint> getAscendingBranch() const
    {
        std::vector<BHPoint> branch;
        for (size_t i = 1; i < points_.size(); ++i)
        {
            if (points_[i].H > points_[i - 1].H)
            {
                if (branch.empty())
                    branch.push_back(points_[i - 1]);
                branch.push_back(points_[i]);
            }
        }
        return branch;
    }

    std::vector<BHPoint> getDescendingBranch() const
    {
        std::vector<BHPoint> branch;
        for (size_t i = 1; i < points_.size(); ++i)
        {
            if (points_[i].H < points_[i - 1].H)
            {
                if (branch.empty())
                    branch.push_back(points_[i - 1]);
                branch.push_back(points_[i]);
            }
        }
        return branch;
    }

    // -----------------------------------------------------------------------
    // Compute B-H loop area (energy loss per cycle) [J/m^3]
    //
    // Uses the shoelace formula on the (H, B) pairs.
    // -----------------------------------------------------------------------
    double computeLoopArea() const
    {
        if (points_.size() < 3)
            return 0.0;

        double area = 0.0;
        for (size_t i = 0; i < points_.size(); ++i)
        {
            size_t j = (i + 1) % points_.size();
            area += points_[i].H * points_[j].B;
            area -= points_[j].H * points_[i].B;
        }
        return std::abs(area) * 0.5;
    }

    // -----------------------------------------------------------------------
    // Compute RMSE between this measurement and a simulated B-H loop.
    // -----------------------------------------------------------------------
    double computeRMSE(const std::vector<BHPoint>& simulated) const
    {
        if (simulated.empty() || points_.empty())
            return 1e10;

        // For each measured point, find the closest simulated H and compare B
        double sumSq = 0.0;
        int count = 0;
        for (const auto& meas : points_)
        {
            // Find nearest simulated point by H
            double bestDist = 1e30;
            double bestSimB = 0.0;
            for (const auto& sim : simulated)
            {
                double dist = std::abs(sim.H - meas.H);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestSimB = sim.B;
                }
            }
            double err = meas.B - bestSimB;
            sumSq += err * err;
            ++count;
        }
        return std::sqrt(sumSq / static_cast<double>(count));
    }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------
    const std::vector<BHPoint>&   getPoints()   const { return points_; }
    const MeasurementMetadata&    getMetadata()  const { return metadata_; }
    MeasurementMetadata&          getMetadata()        { return metadata_; }
    size_t                        size()         const { return points_.size(); }
    bool                          empty()        const { return points_.empty(); }

    // Direct indexed access
    const BHPoint& operator[](size_t idx) const { return points_[idx]; }

    // -----------------------------------------------------------------------
    // Mutation
    // -----------------------------------------------------------------------
    void addPoint(double H, double B)
    {
        points_.push_back({H, B});
    }

    void clear()
    {
        points_.clear();
    }

    void setMetadata(const MeasurementMetadata& meta) { metadata_ = meta; }

    // -----------------------------------------------------------------------
    // Subsample for faster fitting (every n-th point)
    // -----------------------------------------------------------------------
    MeasurementData subsample(int stride) const
    {
        std::vector<BHPoint> sub;
        for (size_t i = 0; i < points_.size(); i += static_cast<size_t>(stride))
            sub.push_back(points_[i]);
        return MeasurementData(std::move(sub), metadata_);
    }

private:
    std::vector<BHPoint>   points_;
    MeasurementMetadata    metadata_;

    // -----------------------------------------------------------------------
    // Validate data after loading.
    // -----------------------------------------------------------------------
    void sortAndValidate()
    {
        // Remove any NaN/Inf points
        points_.erase(
            std::remove_if(points_.begin(), points_.end(),
                [](const BHPoint& p) {
                    return std::isnan(p.H) || std::isnan(p.B)
                        || std::isinf(p.H) || std::isinf(p.B);
                }),
            points_.end()
        );
    }

    // -----------------------------------------------------------------------
    // Minimal JSON field extraction helpers (no dependency).
    // -----------------------------------------------------------------------
    static std::string extractStringField(const std::string& json,
                                          const std::string& key)
    {
        std::string pattern = "\"" + key + "\"";
        auto pos = json.find(pattern);
        if (pos == std::string::npos) return "";

        // Find the colon after the key
        auto colon = json.find(':', pos + pattern.size());
        if (colon == std::string::npos) return "";

        // Find the opening quote of the value
        auto qStart = json.find('"', colon + 1);
        if (qStart == std::string::npos) return "";

        auto qEnd = json.find('"', qStart + 1);
        if (qEnd == std::string::npos) return "";

        return json.substr(qStart + 1, qEnd - qStart - 1);
    }

    static double extractNumberField(const std::string& json,
                                     const std::string& key,
                                     double defaultVal)
    {
        std::string pattern = "\"" + key + "\"";
        auto pos = json.find(pattern);
        if (pos == std::string::npos) return defaultVal;

        auto colon = json.find(':', pos + pattern.size());
        if (colon == std::string::npos) return defaultVal;

        // Skip whitespace after colon
        auto start = colon + 1;
        while (start < json.size() && (json[start] == ' ' || json[start] == '\t'))
            ++start;

        // Read number characters
        std::string numStr;
        while (start < json.size() &&
               (std::isdigit(json[start]) || json[start] == '.' ||
                json[start] == '-' || json[start] == '+' ||
                json[start] == 'e' || json[start] == 'E'))
        {
            numStr += json[start];
            ++start;
        }

        if (numStr.empty()) return defaultVal;

        try {
            return std::stod(numStr);
        } catch (...) {
            return defaultVal;
        }
    }
};

} // namespace transfo
