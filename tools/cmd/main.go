// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package main

import (
	"alatty/tools/cli"
	"alatty/tools/cmd/tool"
)

func main() {
	root := cli.NewRootCommand()
	root.ShortDescription = "Fast, statically compiled implementations of various kittens (command line tools for use with alatty)"
	root.HelpText = "kitten serves as a launcher for running individual kittens. Each kitten can be run as :code:`kitten command`. The list of available kittens is given below."
	root.Usage = "command [command options] [command args]"
	root.Run = func(cmd *cli.Command, args []string) (int, error) {
		return 0, nil
	}

	tool.AlattyToolEntryPoints(root)

	root.Exec()
}
