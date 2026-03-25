#include <catch2/catch_test_macros.hpp>

#include <draxul/citydb.h>
#include <draxul/treesitter.h>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace draxul
{

namespace
{

class SqliteReadHandle final
{
public:
    explicit SqliteReadHandle(const std::filesystem::path& path)
    {
        REQUIRE(sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    }

    ~SqliteReadHandle()
    {
        if (db_)
            sqlite3_close(db_);
    }

    [[nodiscard]] sqlite3* get() const
    {
        return db_;
    }

private:
    sqlite3* db_ = nullptr;
};

int scalar_int(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const int value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

std::string scalar_text(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const unsigned char* value = sqlite3_column_text(stmt, 0);
    const std::string text = value ? reinterpret_cast<const char*>(value) : "";
    sqlite3_finalize(stmt);
    return text;
}

} // namespace

TEST_CASE("city database reconciles tree-sitter snapshot into semantic tables", "[citydb]")
{
    const auto db_path = std::filesystem::temp_directory_path() / "draxul-citydb-tests.sqlite3";
    std::filesystem::remove(db_path);

    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "src/example.cpp";
    file.symbols = {
        { SymbolKind::Class, "Tower", "", false, 10, 30, 3, { "IView", "PlainData" } },
        { SymbolKind::Struct, "PlainData", "", false, 20, 24, 2, {} },
        { SymbolKind::Class, "IView", "", true, 30, 35, 1, { "PlainData" } },
        { SymbolKind::Function, "build_city", "", false, 40, 45, 0, {} },
        { SymbolKind::Function, "tick", "Tower", false, 50, 56, 0, {} },
    };
    snapshot.files.push_back(file);

    CityDatabase db;
    REQUIRE(db.open(db_path));
    REQUIRE(db.reconcile_snapshot(snapshot));

    const CityDbStats& stats = db.stats();
    CHECK(stats.file_count == 1);
    CHECK(stats.symbol_count == 5);
    CHECK(stats.city_entity_count == 4);
    CHECK(stats.has_reconciled_snapshot);

    SqliteReadHandle raw(db_path);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM files") == 1);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM symbols") == 5);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM city_entities") == 4);
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'Tower'")
        == "concrete_class");
    CHECK(scalar_text(raw.get(),
              "SELECT module_path FROM city_entities WHERE display_name = 'Tower'")
        == "src");
    CHECK(scalar_text(raw.get(),
              "SELECT entity_kind FROM city_entities WHERE display_name = 'build_city'")
        == "tree");
    CHECK(scalar_int(raw.get(),
              "SELECT base_size FROM city_entities WHERE display_name = 'Tower'")
        == 3);
    CHECK(scalar_int(raw.get(),
              "SELECT building_functions FROM city_entities WHERE display_name = 'Tower'")
        == 1);
    CHECK(scalar_text(raw.get(),
              "SELECT building_function_sizes_json FROM city_entities WHERE display_name = 'Tower'")
        == "[7]");
    CHECK(scalar_int(raw.get(),
              "SELECT road_size FROM city_entities WHERE display_name = 'Tower'")
        == 2);
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'IView'")
        == "abstract_class");
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'PlainData'")
        == "data_struct");
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'Tower::tick'")
        == "method");
    CHECK(scalar_text(raw.get(),
              "SELECT entity_kind FROM city_entities WHERE display_name = 'Tower'")
        == "building");

    const std::vector<std::string> modules = db.list_modules();
    REQUIRE(modules.size() == 1);
    CHECK(modules[0] == "src");

    const std::vector<CityClassRecord> classes = db.list_classes_in_module("src");
    REQUIRE(classes.size() == 3);
    CHECK(classes[0].module_path == "src");
    CHECK(classes[1].qualified_name == "PlainData");
    CHECK(classes[2].qualified_name == "Tower");
    CHECK(classes[2].base_size == 3);
    CHECK(classes[2].building_functions == 1);
    REQUIRE(classes[2].function_sizes.size() == 1);
    CHECK(classes[2].function_sizes[0] == 7);
    CHECK(classes[2].road_size == 2);

    std::filesystem::remove(db_path);
}

} // namespace draxul
