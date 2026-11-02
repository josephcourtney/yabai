"""Checks for the read-only sublayer audit and guarded transition probe."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "qualify_layout_layers.py"
SPEC = importlib.util.spec_from_file_location("qualify_layout_layers", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class LayerAuditTests(unittest.TestCase):
    def test_flags_below_sublevel_on_float_space_only(self):
        spaces = [
            {"id": 4, "index": 3, "type": "float"},
            {"id": 50, "index": 4, "type": "bsp"},
        ]
        windows = [
            {"id": 106, "space": 3, "app": "kitty",
             "layer": "normal", "sub-layer": "below", "sub-level": -20},
            {"id": 1934, "space": 3, "app": "Zen",
             "layer": "normal", "sub-layer": "normal", "sub-level": 0},
            {"id": 1213, "space": 4, "app": "kitty",
             "layer": "normal", "sub-layer": "below", "sub-level": -20},
            {"id": 500, "space": 3, "app": "utility",
             "layer": "above", "sub-layer": "below", "sub-level": -20},
        ]
        self.assertEqual(
            module.float_space_anomalies(spaces, windows),
            [{"space": 3, "id": 106, "app": "kitty", "sub_level": -20}],
        )

    def test_candidates_exclude_floats_and_unlowered(self):
        windows = [
            {"id": 1, "space": 4, "layer": "normal",
             "sub-layer": "below", "sub-level": -20, "is-floating": False},
            {"id": 2, "space": 4, "layer": "normal",
             "sub-layer": "below", "sub-level": -20, "is-floating": True},
            {"id": 3, "space": 4, "layer": "normal",
             "sub-layer": "normal", "sub-level": 0, "is-floating": False},
            {"id": 4, "space": 3, "layer": "normal",
             "sub-layer": "below", "sub-level": -20, "is-floating": False},
        ]
        self.assertEqual(module.managed_candidates(windows, 4), {1})

    def test_missing_window_is_reported(self):
        self.assertEqual(
            module.layer_results([{"id": 3, "sub-level": 0, "sub-layer": "normal"}], {3, 4}),
            {
                3: {"present": True, "sub_level": 0, "sub_layer": "normal"},
                4: {"present": False, "sub_level": None, "sub_layer": None},
            },
        )


class MutationSafetyTests(unittest.TestCase):
    def setUp(self):
        self.space = {"id": 50, "index": 4, "type": "bsp", "has-focus": True}
        self.window = {"id": 77, "space": 4, "is-floating": False,
                       "layer": "normal", "sub-layer": "below", "sub-level": -20}

    def test_cycle_restores_original_layout_and_checks_layers(self):
        midspace = dict(self.space, type="float")
        midwindow = dict(self.window, **{"sub-level": 0, "sub-layer": "normal"})
        with (
            mock.patch.object(module.subprocess, "run") as preflight,
            mock.patch.object(module, "command") as command,
            mock.patch.object(module, "snapshot", side_effect=[
                ([self.space], [self.window]),
                ([midspace], [midwindow]),
                ([self.space], [self.window]),
            ]),
            mock.patch.object(module.time, "sleep"),
            mock.patch("builtins.print"),
        ):
            self.assertEqual(module.qualify_cycle(Path("/tmp/yabai"), 4, 1.0), 0)
            preflight.assert_called_once()
            self.assertEqual(
                command.call_args_list,
                [
                    mock.call(Path("/tmp/yabai"), "--check-sa"),
                    mock.call(Path("/tmp/yabai"), "-m", "space", "4", "--layout", "float"),
                    mock.call(Path("/tmp/yabai"), "-m", "space", "4", "--layout", "bsp"),
                ],
            )

    def test_exception_during_float_probe_still_restores(self):
        with (
            mock.patch.object(module.subprocess, "run"),
            mock.patch.object(module, "command") as command,
            mock.patch.object(module, "snapshot", side_effect=[
                ([self.space], [self.window]), ValueError("bad query"),
            ]),
            mock.patch.object(module.time, "sleep"),
        ):
            with self.assertRaisesRegex(ValueError, "bad query"):
                module.qualify_cycle(Path("/tmp/yabai"), 4, 1.0)
            self.assertEqual(
                command.call_args_list[-1],
                mock.call(Path("/tmp/yabai"), "-m", "space", "4", "--layout", "bsp"),
            )

    def test_float_command_failure_still_attempts_restoration(self):
        with (
            mock.patch.object(module.subprocess, "run"),
            mock.patch.object(module, "command", side_effect=[
                "", RuntimeError("IPC interrupted"), "",
            ]) as command,
            mock.patch.object(module, "snapshot", return_value=([self.space], [self.window])),
            mock.patch.object(module.time, "sleep"),
        ):
            with self.assertRaisesRegex(RuntimeError, "IPC interrupted"):
                module.qualify_cycle(Path("/tmp/yabai"), 4, 1.0)
            self.assertEqual(command.call_args_list[-1],
                             mock.call(Path("/tmp/yabai"), "-m", "space", "4",
                                       "--layout", "bsp"))

    def test_refuses_nonfocused_space_without_mutating(self):
        other = dict(self.space, **{"has-focus": False})
        with (
            mock.patch.object(module.subprocess, "run"),
            mock.patch.object(module, "command") as command,
            mock.patch.object(module, "snapshot", return_value=([other], [self.window])),
        ):
            with self.assertRaisesRegex(RuntimeError, "active"):
                module.qualify_cycle(Path("/tmp/yabai"), 4, 1.0)
            self.assertEqual(command.call_args_list, [
                mock.call(Path("/tmp/yabai"), "--check-sa"),
            ])



class SourceWiringTests(unittest.TestCase):
    def test_restore_precedes_tree_clear(self):
        source = (SCRIPT.parents[0] / "src" / "space_manager.c").read_text()
        body = source.split("void space_manager_set_layout_for_space(", 1)[1].split("\n}\n", 1)[0]
        self.assertLess(
            body.index("space_manager_restore_layers_before_float(view)"),
            body.index("view_clear(view)"),
        )

    def test_global_and_scoped_config_use_shared_setter(self):
        space_src = (SCRIPT.parents[0] / "src" / "space_manager.c").read_text()
        message_src = (SCRIPT.parents[0] / "src" / "message.c").read_text()
        self.assertIn(
            "space_manager_set_layout_for_space(sm, view->sid, layout)",
            space_src,
        )
        for layout in ("VIEW_BSP", "VIEW_STACK", "VIEW_FLOAT"):
            self.assertIn(
                "space_manager_set_layout_for_space(&g_space_manager, sel_sid, " +
                layout + ")",
                message_src,
            )



if __name__ == "__main__":
    unittest.main()
