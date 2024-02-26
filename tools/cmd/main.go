// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package main

import (
	"os"

	"alatty/kittens/ssh"
	"alatty/tools/cli"
	"alatty/tools/cmd/completion"
	"alatty/tools/cmd/tool"
)

func main() {
	krm := os.Getenv("ALATTY_KITTEN_RUN_MODULE")
	os.Unsetenv("ALATTY_KITTEN_RUN_MODULE")
	switch krm {
	case "ssh_askpass":
		ssh.RunSSHAskpass()
		return
	}
	root := cli.NewRootCommand()
	root.ShortDescription = "Fast, statically compiled implementations of various kittens (command line tools for use with alatty)"
	root.HelpText = "kitten serves as a launcher for running individual kittens. Each kitten can be run as :code:`kitten command`. The list of available kittens is given below."
	root.Usage = "command [command options] [command args]"
	root.Run = func(cmd *cli.Command, args []string) (int, error) {
		cmd.ShowHelp()
		return 0, nil
	}

	tool.AlattyToolEntryPoints(root)
	completion.EntryPoint(root)

	root.Exec()
}
