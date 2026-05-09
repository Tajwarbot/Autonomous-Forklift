from __future__ import annotations

import argparse
import random
import tkinter as tk
from dataclasses import dataclass
from enum import Enum


class ProductColor(str, Enum):
    RED = "red"
    GREEN = "green"
    BLUE = "blue"


@dataclass
class SimConfig:
    speed_px_per_tick: float = 3.2
    tick_ms: int = 25
    missions: int = 6
    obstacle_probability_per_leg: float = 0.20
    classify_correct_probability: float = 0.94
    classify_uncertain_probability: float = 0.05
    max_retake_attempts: int = 2
    lift_success_probability: float = 0.98
    drop_success_probability: float = 0.98


class VisualRobotSimulation:
    def __init__(self, root: tk.Tk, cfg: SimConfig, rng: random.Random) -> None:
        self.root = root
        self.cfg = cfg
        self.rng = rng

        self.width = 1050
        self.height = 680
        self.canvas = tk.Canvas(root, width=self.width, height=self.height, bg="white")
        self.canvas.pack(fill=tk.BOTH, expand=True)

        self.hub = (220, 350)
        self.spine_x = self.hub[0]
        self.pickups = {
            "Pickup-1": (100, 220),
            "Pickup-2": (100, 350),
            "Pickup-3": (100, 480),
        }
        self.docks = {
            ProductColor.RED: ("Dock-A", (820, 220), "#ff6b6b"),
            ProductColor.GREEN: ("Dock-B", (820, 350), "#51cf66"),
            ProductColor.BLUE: ("Dock-C", (820, 480), "#4dabf7"),
        }

        self.robot_size = 34
        self.fork_raised = False
        self.carrying_color: ProductColor | None = None
        self.robot_x, self.robot_y = self.hub
        self.current_target = self.hub
        self.route_points: list[tuple[float, float]] = []
        self.state = "INIT"
        self.mission_idx = 0
        self.success_count = 0
        self.fail_count = 0
        self.current_product: ProductColor | None = None
        self.classified_color: ProductColor | None = None
        self.current_pickup_name = ""
        self.current_pickup: tuple[float, float] = self.pickups["Pickup-2"]
        self.pause_ticks = 0
        self.status = "Booting..."
        self.path_blocked = False
        self.mission_failed = False

        self._draw_static_scene()
        self._start_next_mission()
        self._tick()

    def _draw_static_scene(self) -> None:
        # Track and zones
        self.canvas.create_text(30, 20, anchor="nw", font=("Segoe UI", 11, "bold"), text="Vision-Guided Autonomous Material Handling Robot")
        self.canvas.create_rectangle(50, 90, 980, 610, outline="#adb5bd", width=2)

        # Structured guide path (orthogonal lane map)
        y_values = [pos[1] for pos in self.pickups.values()] + [dock_pos[1] for _, dock_pos, _ in self.docks.values()]
        top_y = min(y_values) - 60
        bottom_y = max(y_values) + 60
        self.canvas.create_line(self.spine_x, top_y, self.spine_x, bottom_y, fill="#212529", width=6)

        for name, (x, y) in self.pickups.items():
            self.canvas.create_line(x, y, self.spine_x, y, fill="black", width=4)
            self.canvas.create_oval(x - 28, y - 28, x + 28, y + 28, fill="#dee2e6", outline="#868e96", width=2)
            self.canvas.create_text(x, y + 42, text=name, font=("Segoe UI", 10, "bold"))

        # Junction marker (replaces home dock)
        self.canvas.create_rectangle(
            self.hub[0] - 34,
            self.hub[1] - 20,
            self.hub[0] + 34,
            self.hub[1] + 20,
            fill="#ffe066",
            outline="#e67700",
            width=2,
        )
        self.canvas.create_text(self.hub[0], self.hub[1] + 38, text="Hub Junction", font=("Segoe UI", 10, "bold"))

        # Docks
        for color, (name, (x, y), fill) in self.docks.items():
            self.canvas.create_line(self.spine_x, y, x, y, fill="black", width=4)
            self.canvas.create_rectangle(x - 55, y - 45, x + 55, y + 45, fill=fill, outline="#343a40", width=2)
            self.canvas.create_text(x, y - 58, text=f"{name} ({color.value})", font=("Segoe UI", 10, "bold"))

        # Overlay elements (robot, obstacle, product, text)
        self.obstacle_id = self.canvas.create_oval(-100, -100, -80, -80, fill="#fa5252", outline="#c92a2a", width=2)
        self.robot_id = self.canvas.create_rectangle(0, 0, 0, 0, fill="#343a40", outline="#111")
        self.fork_id = self.canvas.create_rectangle(0, 0, 0, 0, fill="#adb5bd", outline="#495057")
        self.product_id = self.canvas.create_oval(-50, -50, -30, -30, fill="#ffffff", outline="#222")
        self.status_id = self.canvas.create_text(30, 635, anchor="w", font=("Consolas", 11), text="")
        self.summary_id = self.canvas.create_text(30, 655, anchor="w", font=("Consolas", 11, "bold"), text="")

    def _set_status(self, text: str) -> None:
        self.status = text

    def _route_via_spine(self, start: tuple[float, float], end: tuple[float, float]) -> list[tuple[float, float]]:
        candidates = [(self.spine_x, start[1]), (self.spine_x, end[1]), end]
        route: list[tuple[float, float]] = []
        prev = start
        for point in candidates:
            if point != prev:
                route.append(point)
                prev = point
        return route

    def _set_route(self, start: tuple[float, float], end: tuple[float, float]) -> None:
        self.route_points = self._route_via_spine(start, end)
        self.current_target = self.route_points.pop(0) if self.route_points else end

    def _move_towards_target(self) -> bool:
        tx, ty = self.current_target
        dx = tx - self.robot_x
        dy = ty - self.robot_y
        dist = (dx * dx + dy * dy) ** 0.5
        if dist < self.cfg.speed_px_per_tick:
            self.robot_x, self.robot_y = tx, ty
            if self.route_points:
                self.current_target = self.route_points.pop(0)
                return False
            return True
        self.robot_x += self.cfg.speed_px_per_tick * dx / dist
        self.robot_y += self.cfg.speed_px_per_tick * dy / dist
        return False

    def _maybe_set_obstacle(self, start: tuple[float, float], end: tuple[float, float]) -> None:
        if self.rng.random() < self.cfg.obstacle_probability_per_leg:
            ox = (start[0] + end[0]) / 2 + self.rng.uniform(-30, 30)
            oy = (start[1] + end[1]) / 2 + self.rng.uniform(-25, 25)
            self.canvas.coords(self.obstacle_id, ox - 14, oy - 14, ox + 14, oy + 14)
            self.path_blocked = True
        else:
            self.canvas.coords(self.obstacle_id, -100, -100, -80, -80)
            self.path_blocked = False

    def _classify(self, true_color: ProductColor) -> ProductColor | None:
        for _ in range(self.cfg.max_retake_attempts + 1):
            p = self.rng.random()
            if p < self.cfg.classify_uncertain_probability:
                self._set_status("Vision: uncertain color, retaking image...")
                self._draw_frame()
                self.root.update_idletasks()
                continue
            if p < self.cfg.classify_uncertain_probability + self.cfg.classify_correct_probability:
                return true_color
            return self.rng.choice([c for c in ProductColor if c != true_color])
        return None

    def _start_next_mission(self) -> None:
        self.mission_idx += 1
        if self.mission_idx > self.cfg.missions:
            self.state = "DONE"
            self._set_status("Simulation complete.")
            return

        self.current_product = self.rng.choice(list(ProductColor))
        self.classified_color = None
        self.mission_failed = False
        self.carrying_color = None
        self.fork_raised = False
        self.robot_x, self.robot_y = self.hub
        self.current_pickup_name, self.current_pickup = self.rng.choice(list(self.pickups.items()))
        self._set_route(self.hub, self.current_pickup)
        self._maybe_set_obstacle(self.hub, self.current_pickup)
        self.state = "GO_PICKUP"
        self._set_status(
            f"Mission {self.mission_idx}: Following lane to {self.current_pickup_name} for {self.current_product.value} product"
        )

    def _tick(self) -> None:
        if self.pause_ticks > 0:
            self.pause_ticks -= 1
            self._draw_frame()
            self.root.after(self.cfg.tick_ms, self._tick)
            return

        if self.state == "DONE":
            self._draw_frame()
            return

        if self.state == "GO_PICKUP":
            if self.path_blocked:
                self._set_status("Ultrasonic stop: obstacle detected, waiting to clear...")
                self.pause_ticks = 30
                self.path_blocked = False
                self.canvas.coords(self.obstacle_id, -100, -100, -80, -80)
            elif self._move_towards_target():
                self.state = "CLASSIFY"
                self.pause_ticks = 10
                self._set_status("At pickup: capture image and classify color")

        elif self.state == "CLASSIFY":
            assert self.current_product is not None
            self.classified_color = self._classify(self.current_product)
            if self.classified_color is None:
                self.mission_failed = True
                self.fail_count += 1
                self.state = "RETURN_HUB"
                self._set_route((self.robot_x, self.robot_y), self.hub)
                self._set_status("Vision failed repeatedly: safe-stop and return to hub")
            elif self.rng.random() > self.cfg.lift_success_probability:
                self.mission_failed = True
                self.fail_count += 1
                self.state = "RETURN_HUB"
                self._set_route((self.robot_x, self.robot_y), self.hub)
                self._set_status("Fork-lifter alignment issue: mission failed, returning to hub")
            else:
                self.carrying_color = self.current_product
                self.fork_raised = True
                _, dock_target, _ = self.docks[self.classified_color]
                self._set_route((self.robot_x, self.robot_y), dock_target)
                self._maybe_set_obstacle(self.current_pickup, dock_target)
                self.state = "GO_DOCK"
                self._set_status(f"Classified as {self.classified_color.value}; heading to destination dock")

        elif self.state == "GO_DOCK":
            if self.path_blocked:
                self._set_status("Ultrasonic stop en route to dock, waiting...")
                self.pause_ticks = 30
                self.path_blocked = False
                self.canvas.coords(self.obstacle_id, -100, -100, -80, -80)
            elif self._move_towards_target():
                if self.rng.random() > self.cfg.drop_success_probability:
                    self.mission_failed = True
                    self.fail_count += 1
                    self._set_status("Drop action failed; returning to hub")
                else:
                    delivered_correctly = self.classified_color == self.current_product
                    if delivered_correctly:
                        self.success_count += 1
                        self._set_status("Product placed in correct dock. Returning to hub.")
                    else:
                        self.mission_failed = True
                        self.fail_count += 1
                        self._set_status("Product placed in wrong dock (misclassification). Returning to hub.")
                self.carrying_color = None
                self.fork_raised = False
                self._set_route((self.robot_x, self.robot_y), self.hub)
                self._maybe_set_obstacle((self.robot_x, self.robot_y), self.hub)
                self.state = "RETURN_HUB"
                self.pause_ticks = 8

        elif self.state == "RETURN_HUB":
            if self._move_towards_target():
                self.pause_ticks = 15
                self._start_next_mission()

        self._draw_frame()
        self.root.after(self.cfg.tick_ms, self._tick)

    def _draw_frame(self) -> None:
        x = self.robot_x
        y = self.robot_y
        s = self.robot_size
        self.canvas.coords(self.robot_id, x - s / 2, y - s / 2, x + s / 2, y + s / 2)

        fork_offset = -20 if self.fork_raised else -6
        self.canvas.coords(self.fork_id, x + s / 2 - 2, y + fork_offset, x + s / 2 + 18, y + fork_offset + 6)

        if self.carrying_color is None:
            self.canvas.coords(self.product_id, -50, -50, -30, -30)
        else:
            pfill = {
                ProductColor.RED: "#fa5252",
                ProductColor.GREEN: "#40c057",
                ProductColor.BLUE: "#339af0",
            }[self.carrying_color]
            self.canvas.itemconfig(self.product_id, fill=pfill)
            self.canvas.coords(self.product_id, x - 7, y - 28, x + 7, y - 14)

        progress = f"Mission {min(self.mission_idx, self.cfg.missions)}/{self.cfg.missions}"
        self.canvas.itemconfig(self.status_id, text=f"Status: {self.status}")
        self.canvas.itemconfig(
            self.summary_id,
            text=f"{progress} | Success={self.success_count} | Failed={self.fail_count} | Mapping: red->Dock-A, green->Dock-B, blue->Dock-C",
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Visual simulation of the autonomous material handling robot.")
    parser.add_argument("--missions", type=int, default=6, help="Number of missions to animate.")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for reproducible visuals.")
    args = parser.parse_args()

    cfg = SimConfig(missions=args.missions)
    rng = random.Random(args.seed)

    root = tk.Tk()
    root.title("Autonomous Material Handling Robot - Visual Simulation")
    VisualRobotSimulation(root, cfg, rng)
    root.mainloop()


if __name__ == "__main__":
    main()
