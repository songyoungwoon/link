"""Generate the photo corridor and an original, geometric Hello World destination.

Photographs remain read-only inputs. The inventory determines the corridor size.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
WORLDS = REPOSITORY / "native" / "worlds"


def value(data):
    return json.dumps(data, ensure_ascii=False, separators=(",", ":"))


def number(data):
    return round(float(data), 5)


def vector(data):
    return [number(entry) for entry in data]


class Scene:
    def __init__(self):
        self.nodes = []
        self.images = []
        self.views = []

    def node(self, kind, identifier, parent=None, **fields):
        node = {"kind": kind, "id": identifier, "fields": fields, "children": []}
        (self.nodes if parent is None else parent["children"]).append(node)
        return node

    def component(self, identifier, parent, **fields):
        return self.node("component", identifier, parent, **fields)

    def element(self, identifier, parent, geometry, material, **fields):
        return self.node("element", identifier, parent, geometry=geometry, style=material, **fields)

    def box(self, identifier, parent, size, position, material, **fields):
        return self.element(identifier, parent, {"type": "box", "size": vector(size)}, material, position=vector(position), **fields)

    def plane(self, identifier, parent, size, position, material, **fields):
        return self.element(identifier, parent, {"type": "plane", "size": vector(size)}, material, position=vector(position), **fields)

    def sphere(self, identifier, parent, radius, position, material, **fields):
        return self.element(identifier, parent, {"type": "sphere", "radius": number(radius)}, material, position=vector(position), **fields)

    def cylinder(self, identifier, parent, radius, radius_top, height, position, material, **fields):
        geometry = {"type": "cylinder", "radius": number(radius), "radiusTop": number(radius_top), "height": number(height)}
        return self.element(identifier, parent, geometry, material, position=vector(position), **fields)

    def view(self, name, position, target):
        self.views.append({"name": name, "position": vector(position), "target": vector(target)})

    def render(self):
        count = 0
        identifiers = set()

        def emit(node, depth):
            nonlocal count
            count += 1
            if node["id"] in identifiers:
                raise ValueError("Duplicate generated ID: " + node["id"])
            identifiers.add(node["id"])
            indent = "  " * depth
            lines = [f'{indent}{node["kind"]} {node["id"]} {{']
            lines.extend(f"{indent}  {key}: {value(item)};" for key, item in node["fields"].items())
            for child in node["children"]:
                lines.extend(emit(child, depth + 1))
            return lines + [indent + "}"]

        lines = ["// Generated native Element geometry. Edit generator for repeatable changes.", "scene {"]
        for root in self.nodes:
            lines.extend(emit(root, 1))
        lines.append("}")
        source = "\n".join(lines) + "\n"
        if count > 500 or len(source.encode("utf-8")) > 1048576 or len(self.views) > 128:
            raise ValueError(f"Native input budget exceeded: {count} nodes, {len(self.views)} views")
        return source, count


def write_world(folder, scene, title, entry, appearance, background, **manifest_options):
    source, count = scene.render()
    folder.mkdir(parents=True, exist_ok=True)
    manifest = {"version": "0.1", "id": folder.name, "name": title,
                "elements": "scene.element", "style": "appearance.style", "scripts": [],
                "entry": entry, "environment": {"background": background, "grid": False},
                "views": scene.views, **manifest_options}
    (folder / "scene.element").write_text(source, encoding="utf-8")
    (folder / "appearance.style").write_text(appearance.strip() + "\n", encoding="utf-8")
    (folder / "world.link").write_text("world {\n" + "\n".join(f"  {key}: {value(item)};" for key, item in manifest.items()) + "\n}\n", encoding="utf-8")
    print(f"Generated {folder.name}: {count} nodes, {len(scene.images)} photos, {len(scene.views)} camera views")


def gallery():
    folder = WORLDS / "heukryong"
    inventory = json.loads((folder / "inventory.json").read_text(encoding="utf-8-sig"))
    photos = inventory["photographs"]
    cats = [item for item in photos if item["category"] == "cat"]
    owners = [item for item in photos if item["category"] == "owner"]
    if not cats or len(photos) != len(cats) + len(owners):
        raise ValueError("The corridor requires cat photographs and optional personal portraits only.")
    if len(owners) > 2:
        raise ValueError("The entrance has space for at most two personal portraits.")
    if len(photos) > 84:
        raise ValueError("Select at most 84 photos to keep the framed corridor within the native geometry budget.")
    identifiers = [item["asset"] for item in photos]
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("Each photograph must have a unique display-copy path.")

    scene = Scene()
    root = scene.node("object", "heukryong_corridor", name="사진 공간")
    rows = math.ceil(len(cats) / 2)
    portal_z = -(rows * 3.4 + 4)
    front_z, rear_z = 5.4, portal_z - 2.9
    length, center_z = front_z - rear_z, (front_z + rear_z) / 2
    architecture = scene.component("corridor_architecture", root, name="복도")
    scene.box("corridor_floor", architecture, [6.6, .16, length], [0, -.08, center_z], "floor")
    scene.box("corridor_ceiling", architecture, [6.6, .16, length], [0, 3.88, center_z], "ceiling")
    for side, label in [(-1, "left"), (1, "right")]:
        scene.box(f"corridor_wall_{label}", architecture, [.16, 3.8, length], [side * 3.28, 1.9, center_z], "wall")
        scene.box(f"corridor_skirt_{label}", architecture, [.028, .14, length], [side * 3.183, .1, center_z], "oak")
        scene.box(f"corridor_ceiling_trim_{label}", architecture, [.035, .055, length], [side * 3.175, 3.72, center_z], "brass")
    scene.box("corridor_end_wall", architecture, [6.6, 3.8, .16], [0, 1.9, rear_z], "portal_backdrop")
    for row in range(rows + 2):
        z = 2.7 - row * 3.4
        scene.box(f"floor_joint_{row:02}", architecture, [6.36, .006, .018], [0, .004, z], "floor_joint")
        if row % 2 == 0:
            scene.box(f"light_recess_{row:02}", architecture, [1.95, .045, .2], [0, 3.775, z], "oak")
            scene.box(f"light_diffuser_{row:02}", architecture, [1.77, .025, .14], [0, 3.74, z], "light")
    for side in [-1, 1]:
        label = "left" if side < 0 else "right"
        scene.box(f"floor_guide_{label}", architecture, [.025, .008, length - .4], [side * 2.82, .007, center_z], "brass")

    def frame(photo, point, yaw, label, max_width=2.35, max_height=2.22):
        identifier = f'{photo["category"]}_{photo["index"]:03}'
        group = scene.component("frame_" + identifier, root, name=label, position=vector(point), rotation=[0, yaw, 0])
        ratio = photo["width"] / photo["height"]
        display_w = min(max_width - .28, (max_height - .28) * ratio)
        display_h = display_w / ratio
        width, height = display_w + .28, display_h + .28
        scene.element("rim_" + identifier, group, {"type": "frame", "size": vector([width, height]), "border": .055, "depth": .09}, "oak")
        scene.plane("mat_" + identifier, group, [width - .1, height - .1], [0, 0, .047], "mat")
        scene.plane("photo_" + identifier, group, [display_w, display_h], [0, 0, .058], "photograph", image=photo["asset"], name=label)
        angle = math.radians(yaw)
        normal = [math.sin(angle), 0, math.cos(angle)]
        scene.view(label, [point[i] + normal[i] * 3.8 for i in range(3)], point)
        scene.images.append(photo["asset"])

    scene.view("입구", [0, 1.65, 4], [0, 1.65, 0])
    for index, photo in enumerate(cats):
        row, side_index = divmod(index, 2)
        side = -1 if side_index == 0 else 1
        frame(photo, [side * 3.12, 1.72, -2.4 - row * 3.4], 90 if side < 0 else -90, f"사진 {index + 1:03}")
    for index, photo in enumerate(owners):
        side = -1 if index == 0 else 1
        frame(photo, [side * 3.12, 1.72, 2], 90 if side < 0 else -90, f"인물 사진 {index + 1:02}", 1.95, 2.12)
    scene.view("복도 중간", [0, 1.65, -(rows // 2) * 3.4], [0, 1.65, portal_z])
    scene.view("포탈", [0, 1.65, portal_z + 6], [0, 1.65, portal_z])

    portal = scene.component("portal_geometry", root, name="포탈", position=[0, 0, number(portal_z)])
    # Nested solid 3D frames leave the central opening unobstructed.
    for layer, (width, height, depth, material) in enumerate([(3.25, 3.5, .42, "portal_dark"), (2.95, 3.32, .17, "portal_metal"), (2.65, 3.2, .08, "portal_light")]):
        border = .14 if layer == 0 else .07
        z = .08 + layer * .23
        for side, label in [(-1, "left"), (1, "right")]:
            scene.box(f"portal_{layer}_{label}", portal, [border, height, depth], [side * (width - border) / 2, height / 2, z], material)
        scene.box(f"portal_{layer}_top", portal, [width, border, depth], [0, height - border / 2, z], material)
    scene.box("portal_threshold", portal, [3.35, .025, 1.8], [0, .014, .05], "portal_metal")
    scene.box("portal_threshold_light", portal, [2.48, .012, .055], [0, .032, .55], "portal_light")
    for index, x in enumerate([-1.12, -.56, 0, .56, 1.12]):
        scene.box(f"portal_recess_blade_{index}", portal, [.028, 2.66, .025], [x, 1.65, -.95], "portal_soft")

    if len(scene.images) != len(photos) or set(scene.images) != set(identifiers):
        raise ValueError("Every selected photograph must appear exactly once.")
    appearance = """
defaults { color: "#F3EEE5"; roughness: .85; metalness: 0; opacity: 1; wireframe: false; }
material wall { color: "#EFECE5"; }
material ceiling { color: "#E4E0D7"; }
material floor { color: "#CBBFA9"; }
material floor_joint { color: "#B6A991"; }
material oak { color: "#705139"; roughness: .6; }
material brass { color: "#BBA075"; metalness: .35; }
material mat { color: "#FFFCF6"; }
material photograph { color: "#FFFFFF"; roughness: 1; }
material light { color: "#FFF6CA"; }
material portal_backdrop { color: "#213B40"; }
material portal_dark { color: "#214044"; metalness: .2; }
material portal_metal { color: "#60968F"; metalness: .45; }
material portal_light { color: "#8FF4DD"; }
material portal_soft { color: "#4C9D94"; }
"""
    write_world(folder, scene, "사진 공간", {"position": [0, 1.65, 4], "target": [0, 1.65, 0]}, appearance, "#E7E3DA",
                walk={"min": [-2.65, 0, number(portal_z - 2.5)], "max": [2.65, 3.5, 5], "eyeHeight": 1.65, "speed": 3.4},
                portals=[{"id": "hello_portal", "address": "link://local/hello-world", "position": [0, 1.65, number(portal_z)], "size": [2.4, 3.3, 1]}])


# Original seven-row glyphs. Horizontal runs become extruded solid boxes.
GLYPHS = {
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "e": ["00000", "00000", "01110", "10001", "11111", "10000", "01110"],
    "l": ["110", "010", "010", "010", "010", "010", "111"],
    "o": ["00000", "00000", "01110", "10001", "10001", "10001", "01110"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "r": ["00000", "00000", "10110", "11001", "10000", "10000", "10000"],
    "d": ["00001", "00001", "01101", "10011", "10001", "10001", "01111"],
    "!": ["1", "1", "1", "1", "1", "0", "1"],
    " ": ["000"] * 7,
}


def hello_world():
    scene = Scene()
    root = scene.node("object", "hello_world_destination", name="Hello World!")
    architecture = scene.component("hello_architecture", root)
    # A walkable arrival deck suspended above an unbounded night sky.
    # Its vanishing lines lead toward the first message and a monumental aperture.
    scene.box("hello_floor", architecture, [34, .5, 86], [0, -.25, -5], "obsidian")
    scene.box("arrival_causeway", architecture, [10.5, .025, 78], [0, .0125, -1], "path")
    scene.box("horizon_landing", architecture, [33.9, .03, 20], [0, .015, -37], "landing")
    for side, label in [(-1, "left"), (1, "right")]:
        scene.box(f"deck_rim_{label}", architecture, [.3, .48, 86], [side * 16.85, -.05, -5], "stone")
        scene.box(f"deck_edge_light_{label}", architecture, [.055, .022, 85.7], [side * 16.65, .2, -5], "soft_cyan")
        scene.box(f"causeway_line_{label}", architecture, [.055, .012, 75.5], [side * 5.05, .032, -.75], "gold_light")
        scene.box(f"causeway_trace_{label}", architecture, [.022, .01, 75.5], [side * 5.36, .031, -.75], "soft_cyan")
        for index in range(11):
            z = 33 - index * 6.6
            scene.box(f"arrival_joint_{label}_{index:02}", architecture, [11.2, .014, .022], [side * 11.1, .028, z], "joint")
        for index in range(8):
            z = 31 - index * 9.5
            scene.box(f"edge_beacon_base_{label}_{index:02}", architecture, [.4, .7, .52], [side * 15.45, .35, z], "stone")
            scene.box(f"edge_beacon_{label}_{index:02}", architecture, [.29, .055, .4], [side * 15.45, .715, z], "cyan_light")
    for index in range(14):
        z = 32 - index * 5.1
        scene.box(f"causeway_seam_{index:02}", architecture, [9.95, .008, .018], [0, .031, z], "joint")
        scene.box(f"causeway_marker_{index:02}", architecture, [.16, .018, .66], [0, .038, z], "soft_gold")
    scene.box("arrival_threshold", architecture, [10.1, .018, .08], [0, .039, 35], "gold_light")
    scene.cylinder("landing_foundation", architecture, 5.2, 20, 11, [0, -6.1, -29], "abyss_stone", rotation=[0, 45, 0])
    scene.box("causeway_spine", architecture, [7, 2.1, 82], [0, -1.55, -5], "abyss_stone")

    aperture = scene.component("horizon_aperture", root, name="The first horizon", position=[0, 13, -43])
    segments = 48
    for layer, (radius, thickness, depth, z, material) in enumerate([
        (17.15, 1.65, 2.3, 0, "ring_stone"),
        (16.13, .105, .12, 1.03, "cyan_light"),
    ]):
        for index in range(segments):
            angle = 2 * math.pi * index / segments
            tangent_length = 2 * radius * math.tan(math.pi / segments) + (.025 if layer == 0 else -.045)
            scene.box(f"aperture_{layer}_{index:02}", aperture, [tangent_length, thickness, depth],
                      [radius * math.cos(angle), radius * math.sin(angle), z], material,
                      rotation=[0, 0, number(math.degrees(angle) + 90)])
    for index in range(24):
        angle = 2 * math.pi * index / 24
        scene.box(f"aperture_outermark_{index:02}", aperture, [.83, .105, .14],
                  [17.81 * math.cos(angle), 17.81 * math.sin(angle), 1.21], "soft_gold",
                  rotation=[0, 0, number(math.degrees(angle) + 90)])
    for side, label in [(-1, "left"), (1, "right")]:
        scene.box(f"aperture_foot_{label}", architecture, [5.8, .65, 7.6], [side * 15.4, .325, -43], "stone")
        scene.box(f"aperture_anchor_{label}", architecture, [1.4, 10.6, 2.9], [side * 16.9, 5.3, -43], "ring_stone")
        scene.box(f"aperture_anchor_inlay_{label}", architecture, [.065, 7.9, .05], [side * 16.9, 5.8, -41.52], "gold_light")
        scene.box(f"monument_support_{label}", architecture, [.42, 4.75, 1.1], [side * 15.8, 2.375, -24], "stone")
        scene.box(f"monument_support_light_{label}", architecture, [.045, 3.9, .03], [side * 15.8, 2.5, -23.43], "soft_gold")
    scene.box("hello_stage", architecture, [32.5, .22, 1.6], [0, 4.82, -24], "stone")
    scene.box("hello_stage_edge", architecture, [32.4, .045, .045], [0, 4.93, -23.17], "gold_light")

    # Slow orbital registers suggest patient, immense machinery without flashing.
    orbit = scene.component("aperture_orbit", aperture, script="scripts/horizon.script", config={"speed": 1.15})
    for index, degrees in enumerate([18, 102, 193, 281]):
        angle = math.radians(degrees)
        scene.box(f"aperture_register_{index}", orbit, [1.25, .17, .2], [15.68 * math.cos(angle), 15.68 * math.sin(angle), 1.1], "gold_light", rotation=[0, 0, degrees + 90])

    text, cell, depth = "Hello World!", .48, .74
    widths = [len(GLYPHS[letter][0]) for letter in text]
    total_width = (sum(widths) + len(widths) - 1) * cell
    cursor = -total_width / 2
    letters = scene.component("hello_world_text", root, name=text, position=[0, 5.54, -24])
    for index, letter in enumerate(text):
        glyph = GLYPHS[letter]
        if letter != " ":
            group = scene.component(f"hello_glyph_{index:02}", letters, name=letter, position=[number(cursor), 0, 0])
            for row, pattern in enumerate(glyph):
                column, run = 0, 0
                while column < len(pattern):
                    if pattern[column] == "0":
                        column += 1
                        continue
                    start = column
                    while column < len(pattern) and pattern[column] == "1":
                        column += 1
                    width = column - start
                    scene.box(f"hello_stroke_{index:02}_{row}_{run}", group, [width * cell, cell, depth], [(start + width / 2) * cell, (6 - row) * cell, 0], "letter" if index < 6 else "letter_accent")
                    run += 1
        cursor += (len(glyph[0]) + 1) * cell

    # The browser's own text primitive projects the service introduction into
    # the world. The original greeting remains solid, extruded geometry.
    hologram = scene.component("link_hologram", root, name="About LINK", position=[0, 2.35, -11],
                                script="scripts/hologram.script", config={"height": 2.35, "z": -11})
    scene.plane("link_hologram_glass", hologram, [27, 3.55], [0, 0, 0], "holo_glass")
    introduction = ("LINK — Linked Interactive Network\n"
                    "An open standard for virtual worlds: Element · Script · Style.\n"
                    "Explore with LINK Browser. Connect with LINK Protocol.\n"
                    "Every World. One Link.")
    scene.plane("link_hologram_text", hologram, [26.2, 3.5], [0, 0, .035], "holo_text",
                name="LINK · Every World. One Link.", text=introduction)
    for side, label in [(-1, "left"), (1, "right")]:
        scene.box(f"link_hologram_edge_{label}", hologram, [.022, 3.55, .025], [side * 13.5, 0, .015], "holo_edge")
    for side, label in [(-1, "bottom"), (1, "top")]:
        scene.box(f"link_hologram_edge_{label}", hologram, [27, .022, .025], [0, side * 1.775, .015], "holo_edge")
    scene.box("link_hologram_scan", hologram, [26.85, .014, .022], [0, -1.72, .075], "holo_scan")
    scene.box("link_projector_base", architecture, [10, .1, .6], [0, .05, -10.4], "stone")
    scene.box("link_projector_light", architecture, [9.6, .018, .13], [0, .109, -10.25], "cyan_light")
    scene.plane("link_projection_pool", architecture, [13, 3.4], [0, .042, -11], "holo_pool", rotation=[-90, 0, 0])

    # Native geometric scenery: no photograph, character or imported model.
    cosmos = scene.component("distant_horizons", root)
    scene.sphere("horizon_planet", cosmos, 13.2, [-1, 35, -107], "planet")
    scene.sphere("distant_moon", cosmos, 4.1, [-45, 57, -143], "moon")
    planet_rings = scene.component("planet_orbital_plane", cosmos, position=[-1, 35, -107], rotation=[68, 0, -18])
    for index in range(40):
        angle = 2 * math.pi * index / 40
        scene.box(f"planet_ring_{index:02}", planet_rings, [2.59, .11, .11], [16.5 * math.cos(angle), 16.5 * math.sin(angle), 0], "planet_ring", rotation=[0, 0, number(math.degrees(angle) + 90)])
    for index in range(64):
        # Deterministic, non-grid stars keep each generated world reproducible.
        horizontal = (index * .61803398875 + .17) % 1
        vertical = (index * .41421356237 + .39) % 1
        distance = (index * .73205080757 + .23) % 1
        x, y, z = -86 + horizontal * 172, 9 + vertical * 66, -112 - distance * 58
        radius = .18 + ((index * 11) % 9) * .037
        if index % 13 == 0:
            radius = .6
        scene.sphere(f"horizon_star_{index:02}", cosmos, radius, [x, y, z], "star_gold" if index % 7 == 0 else "star")
    for index, (x, y, z, radius, height) in enumerate([
        (-32, -1.4, -35, 7.7, 13), (34, 2.1, -54, 8.8, 17),
        (-51, 6.3, -87, 8.1, 21), (57, 11.4, -116, 11.5, 22),
        (-27, -3.5, -72, 4.8, 12), (25, 5.5, -95, 3.4, 8),
    ]):
        island = scene.component(f"floating_island_{index}", cosmos, position=vector([x, y, z]), rotation=[0, index * 31, 0])
        scene.cylinder(f"island_body_{index}", island, .7, radius, height, [0, -height / 2, 0], "distant_stone")
        scene.cylinder(f"island_crown_{index}", island, radius, radius * .9, .32, [0, .16, 0], "island_top")
        scene.box(f"island_obelisk_{index}", island, [1.25, 5 + index * .55, 1.25], [0, 2.5 + index * .275, 0], "stone")
        scene.box(f"island_signal_{index}", island, [.09, 3.4 + index * .4, .09], [0, 2.6 + index * .275, .65], "soft_cyan")

    entry = {"position": [3.4, 1.65, 32], "target": [0, 10.4, -34]}
    scene.view("Hello World!", entry["position"], entry["target"])
    scene.view("The first horizon", [0, 1.65, 18], [0, 10.6, -40])
    scene.view("A world beyond", [32, 19, 5], [0, 8.5, -33])
    appearance = """
defaults { color: "#244348"; roughness: .64; metalness: .15; opacity: 1; wireframe: false; }
material obsidian { color: "#14282F"; roughness: .58; }
material path { color: "#233E46"; roughness: .65; }
material landing { color: "#1B353E"; }
material joint { color: "#4A5D5F"; }
material stone { color: "#47676C"; metalness: .3; }
material ring_stone { color: "#527A82"; metalness: .45; }
material abyss_stone { color: "#0E252C"; }
material distant_stone { color: "#1C4248"; }
material island_top { color: "#3C6265"; }
material letter { color: "#FFF0CE"; roughness: .35; emission: .48; }
material letter_accent { color: "#B5F7EF"; roughness: .35; emission: .6; }
material cyan_light { color: "#66E9E0"; emission: 2.1; }
material soft_cyan { color: "#69C7C7"; emission: .85; }
material gold_light { color: "#F1CC87"; emission: 1.45; }
material soft_gold { color: "#B9A57F"; emission: .65; }
material planet { color: "#8AB4BD"; roughness: .95; emission: .12; }
material moon { color: "#BAC8BD"; emission: .08; }
material planet_ring { color: "#B1CED0"; emission: .55; }
material star { color: "#B1DBE2"; emission: 1.6; }
material star_gold { color: "#F2D5A5"; emission: 1.3; }
material holo_glass { color: "#34959E"; opacity: .055; emission: .3; }
material holo_text { color: "#A3F9F2"; textBackground: false; opacity: .94; }
material holo_edge { color: "#66DBDE"; opacity: .58; emission: 1.2; }
material holo_scan { color: "#82F5ED"; opacity: .16; emission: 1.1; }
material holo_pool { color: "#51B8C3"; opacity: .085; emission: .45; }
"""
    folder = WORLDS / "hello-world"
    scripts = folder / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    (scripts / "horizon.script").write_text("""// A slow continuous orbit, with no external dependencies.
on onStart(ctx) {
  store angle = 0;
}
on onUpdate(ctx, deltaSeconds) {
  angle = (angle + ctx.config.speed * deltaSeconds) % 360;
  ctx.setTransform({rotation: [0, 0, angle]});
}
""", encoding="utf-8")
    (scripts / "hologram.script").write_text("""// A quiet floating projection, driven by ordinary world transforms.
on onStart(ctx) {
  store phase = 0;
}
on onUpdate(ctx, deltaSeconds) {
  phase = (phase + deltaSeconds * .16) % 2;
  let wave = 1 - abs(phase - 1);
  let drift = wave * wave * (3 - 2 * wave);
  ctx.setTransform({position: [0, ctx.config.height + drift * .045, ctx.config.z]});
  ctx.setTransform("link_hologram_scan", {position: [0, -1.72 + wave * 3.44, .075]});
}
""", encoding="utf-8")
    write_world(folder, scene, text, entry, appearance, "#08171F", scripts=["scripts/horizon.script", "scripts/hologram.script"],
                environment={"background": "#08171F", "grid": False, "fog": {"color": "#08171F", "start": 85, "end": 280}},
                walk={"min": [-15.4, 0, -46], "max": [15.4, 4, 36.5], "eyeHeight": 1.65, "speed": 5.1})


if __name__ == "__main__":
    gallery()
    hello_world()
