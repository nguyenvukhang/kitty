// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package run_shell

import (
	"fmt"
	"alatty"
	"os"
	"strings"

	"alatty/tools/cli"
	"alatty/tools/tui"
)

var _ = fmt.Print

type Options struct {
	Shell            string
	Env              []string
	Cwd              string
}

func main(args []string, opts *Options) (rc int, err error) {
	if len(args) > 0 {
		tui.RunCommandRestoringTerminalToSaneStateAfter(args)
	}
	env_before := os.Environ()
	changed := false
	for _, entry := range opts.Env {
		k, v, found := strings.Cut(entry, "=")
		if found {
			if err := os.Setenv(k, v); err != nil {
				return 1, fmt.Errorf("Failed to set the env var %s with error: %w", k, err)
			}
		} else {
			if err := os.Unsetenv(k); err != nil {
				return 1, fmt.Errorf("Failed to unset the env var %s with error: %w", k, err)
			}
		}
		changed = true
	}
	if os.Getenv("TERM") == "" {
		os.Setenv("TERM", alatty.DefaultTermName)
	}
	err = tui.RunShell(tui.ResolveShell(opts.Shell), opts.Cwd)
	if changed {
		os.Clearenv()
		for _, entry := range env_before {
			k, v, _ := strings.Cut(entry, "=")
			os.Setenv(k, v)
		}
	}
	if err != nil {
		rc = 1
	}
	return
}

func EntryPoint(root *cli.Command) *cli.Command {
	sc := root.AddSubCommand(&cli.Command{
		Name:             "run-shell",
		Usage:            "[options] [optional cmd to run before running the shell ...]",
		ShortDescription: "Run the user's shell with shell integration enabled",
		HelpText:         "Run the users's configured shell. If the shell supports shell integration, enable it based on the user's configured shell_integration setting.",
		Run: func(cmd *cli.Command, args []string) (ret int, err error) {
			opts := &Options{}
			err = cmd.GetOptionValues(opts)
			if err != nil {
				return 1, err
			}
			return main(args, opts)
		},
	})
	sc.Add(cli.OptionSpec{
		Name:    "--shell",
		Default: ".",
	})
	sc.Add(cli.OptionSpec{
		Name: "--env",
		Type: "list",
	})
	sc.Add(cli.OptionSpec{
		Name: "--cwd",
	})

	return sc
}
