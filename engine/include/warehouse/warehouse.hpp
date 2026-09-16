#pragma once

#include "warehouse/types.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace warehouse {

class Warehouse {
public:
    explicit Warehouse(std::vector<std::string> rows);
    static Warehouse load(const std::filesystem::path& path);
    static Warehouse procedural(int width, int height, std::uint32_t seed = 42);
    static Warehouse for_fleet(int robot_count, std::uint32_t seed = 42);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] int cell_count() const { return width_ * height_; }
    [[nodiscard]] bool contains(Position p) const;
    [[nodiscard]] bool traversable(Position p) const;
    [[nodiscard]] CellType at(Position p) const;
    [[nodiscard]] int index(Position p) const { return p.y * width_ + p.x; }
    [[nodiscard]] Position position(int index) const { return {index % width_, index / width_}; }
    [[nodiscard]] std::vector<Position> neighbors(Position p) const;
    [[nodiscard]] const std::vector<std::string>& rows() const { return rows_; }
    [[nodiscard]] const std::vector<Shelf>& shelves() const { return shelves_; }
    [[nodiscard]] const std::vector<PackingStation>& packing_stations() const { return packing_; }
    [[nodiscard]] const std::vector<ChargingStation>& charging_stations() const { return charging_; }
    [[nodiscard]] const std::vector<Position>& floor_positions() const { return floor_; }
    [[nodiscard]] std::vector<int> distance_field(Position goal) const;
    [[nodiscard]] Position nearest_packing(Position from) const;
    [[nodiscard]] Position nearest_charging(Position from) const;

private:
    std::vector<std::string> rows_;
    int width_{};
    int height_{};
    std::vector<Shelf> shelves_;
    std::vector<PackingStation> packing_;
    std::vector<ChargingStation> charging_;
    std::vector<Position> floor_;
};

} // namespace warehouse
