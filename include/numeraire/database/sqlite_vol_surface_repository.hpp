#pragma once

#include <numeraire/database/vol_surface_types.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// Persists one EOD vol surface (header + sparse points) into SQLite.
class SqliteVolSurfaceRepository {
   public:
    explicit SqliteVolSurfaceRepository(std::string database_file_path);

    /// Replaces points for the surface identified by header keys; upserts header metadata.
    [[nodiscard]] long UpsertSurface(const VolSurfaceEodHeaderWrite& header,
                                     const std::vector<VolSurfacePointWrite>& points);

   private:
    std::string database_file_path_;
};

}  // namespace numeraire::database
