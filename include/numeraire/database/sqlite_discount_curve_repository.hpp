#pragma once

#include <numeraire/database/discount_curve_types.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// Persists one bootstrapped EOD discount curve (header + pillar points) into SQLite.
class SqliteDiscountCurveRepository {
   public:
    explicit SqliteDiscountCurveRepository(std::string database_file_path);

    /// Replaces pillar rows for `(curve_id, as_of)`; upserts header metadata.
    void UpsertCurve(const DiscountCurveEodHeaderWrite& header,
                     const std::vector<DiscountCurvePointWrite>& points);

   private:
    std::string database_file_path_;
};

}  // namespace numeraire::database
