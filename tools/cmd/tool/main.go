// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package tool

import (
	"alatty/kittens/ask"
	"alatty/tools/cli"
	"alatty/tools/cmd/run_shell"
	"alatty/tools/cmd/show_error"
	"alatty/tools/tui"
)

func AlattyToolEntryPoints(root *cli.Command) {
	root.Add(cli.OptionSpec{
		Name: "--version", Type: "bool-set", Help: "The current kitten version."})
	tui.PrepareRootCmd(root)
	ask.EntryPoint(root)
	run_shell.EntryPoint(root)
	show_error.EntryPoint(root)
	root.AddSubCommand(&cli.Command{
		Name:            "__hold_till_enter__",
		Hidden:          true,
		Run: func(cmd *cli.Command, args []string) (rc int, err error) {
			tui.ExecAndHoldTillEnter(args)
			return
		},
	})
}
