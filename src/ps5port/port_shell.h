// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../graphics.h"
#include "ui_model.h"

namespace ps5port {

class PortShell {
public:
    void draw(Scene2D& scene, const AppModel& model, int tick) const;

private:
    void backdrop(Scene2D& scene, const AppModel& model) const;
    void rail(Scene2D& scene, const AppModel& model) const;
    void board(Scene2D& scene, const AppModel& model) const;
    void discover(Scene2D& scene, const AppModel& model) const;
    void simplePage(Scene2D& scene, const AppModel& model) const;
    void detail(Scene2D& scene, const AppModel& model) const;
    void hints(Scene2D& scene, const char* value) const;
};

}
