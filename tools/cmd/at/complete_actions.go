// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package at

import (
	"fmt"
	"strings"

	"alatty"
	"alatty/tools/cli"
	"alatty/tools/utils"
)

var _ = fmt.Print

func complete_actions(completions *cli.Completions, word string, arg_num int) {
	if arg_num < 2 {
		scanner := utils.NewLineScanner(AlattyActionNames)
		mg := completions.AddMatchGroup("Actions")
		for scanner.Scan() {
			line := strings.TrimSpace(scanner.Text())
			if line != "" && strings.HasPrefix(line, word) {
				mg.AddMatch(line)
			}
		}
	}
}

func complete_alatty_override(completions *cli.Completions, word string, arg_num int) {
	mg := completions.AddMatchGroup("Config directives")
	mg.NoTrailingSpace = true
	scanner := utils.NewLineScanner(alatty.OptionNames)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, word) {
			mg.AddMatch(line + "=")
		}
	}
}
