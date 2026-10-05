#include "app.h"

namespace oy {

bool App::saveConfig(std::string& err) { return config.save(configPath, err); }

void App::seedDefaults() {
    for (const auto& d : modules.list()) {
        auto& slot = config.slot(d.id);
        if (slot.values.empty() && !d.defaultValues.empty()) {
            slot.values = d.defaultValues;
        }
    }
}

}
