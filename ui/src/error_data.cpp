// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/error_data.hpp"

#include <exception>

namespace medic::ui {

const medic::ErrorDb& embedded_error_db() {
    static const medic::ErrorDb db = [] {
        try {
            return medic::load_error_db_from_string(kErrorCodesJson);
        } catch (const std::exception&) {
            return medic::ErrorDb{};
        }
    }();
    return db;
}

}  // namespace medic::ui
