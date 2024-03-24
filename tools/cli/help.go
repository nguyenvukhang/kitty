// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package cli

import (
	"fmt"
	"os"

	"alatty"
	"alatty/tools/cli/markup"
	"alatty/tools/tty"
)

var _ = fmt.Print

func ShowError(err error) {
	formatter := markup.New(tty.IsTerminal(os.Stderr.Fd()))
	msg := formatter.Prettify(err.Error())
	fmt.Fprintln(os.Stderr, formatter.Err("Error")+":", msg)
}

func (self *Command) version_string(formatter *markup.Context) string {
	return fmt.Sprintln(self.CommandStringForUsage(), formatter.Opt(alatty.VersionString), "created by", formatter.Title("Kovid Goyal"))
}

func (self *Command) ShowVersion() {
	formatter := markup.New(tty.IsTerminal(os.Stdout.Fd()))
	fmt.Fprint(os.Stdout, self.version_string(formatter))
}
