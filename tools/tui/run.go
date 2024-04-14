// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package tui

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/shirou/gopsutil/v3/process"
	"golang.org/x/sys/unix"

	"alatty/tools/tty"
	"alatty/tools/tui/loop"
	"alatty/tools/utils"
	"alatty/tools/utils/shlex"
)

func get_shell_from_alatty_conf() (shell string) {
  s, e := utils.LoginShellForCurrentUser()
  if e != nil {
    return "/bin/sh"
  } else {
    return s
  }
}

func find_shell_parent_process() string {
	var p *process.Process
	var err error
	for {
		if p == nil {
			p, err = process.NewProcess(int32(os.Getppid()))
		} else {
			p, err = p.Parent()
		}
		if err != nil {
			return ""
		}
	}
}

func ResolveShell(shell string) []string {
	switch shell {
	case "":
		shell = get_shell_from_alatty_conf()
	case ".":
		if shell = find_shell_parent_process(); shell == "" {
			shell = get_shell_from_alatty_conf()
		}
	}
	shell_cmd, err := shlex.Split(shell)
	if err != nil {
		shell_cmd = []string{shell}
	}
	exe := utils.FindExe(shell_cmd[0])
	if unix.Access(exe, unix.X_OK) != nil {
		shell_cmd = []string{"/bin/sh"}
	}
	return shell_cmd
}

func RunShell(shell_cmd []string, cwd string) (err error) {
	exe := shell_cmd[0]
	if runtime.GOOS == "darwin" {
		// ensure shell runs in login mode. On macOS lots of people use ~/.bash_profile instead of ~/.bashrc
		// which means they expect the shell to run in login mode always. Le Sigh.
		shell_cmd[0] = "-" + filepath.Base(shell_cmd[0])
	}
	var env []string = os.Environ()
	// fmt.Println(fmt.Sprintf("%s %v\n%#v", utils.FindExe(exe), shell_cmd, env))
	if cwd != "" {
		_ = os.Chdir(cwd)
	}
	return unix.Exec(utils.FindExe(exe), shell_cmd, env)
}

func RunCommandRestoringTerminalToSaneStateAfter(cmd []string) {
	exe := utils.FindExe(cmd[0])
	c := exec.Command(exe, cmd[1:]...)
	c.Stdout = os.Stdout
	c.Stdin = os.Stdin
	c.Stderr = os.Stderr
	term, err := tty.OpenControllingTerm()
	if err == nil {
		var state_before unix.Termios
		if term.Tcgetattr(&state_before) == nil {
			if _, err = term.WriteString(loop.SAVE_PRIVATE_MODE_VALUES); err != nil {
				fmt.Fprintln(os.Stderr, "failed to write to controlling terminal with error:", err)
				return
			}
			defer func() {
				_, _ = term.WriteString(strings.Join([]string{
					loop.RESTORE_PRIVATE_MODE_VALUES,
					"\x1b[=u",                      // reset alatty keyboard protocol to legacy
					"\x1b[1 q",                     // blinking block cursor
					loop.DECTCEM.EscapeCodeToSet(), // cursor visible
					"\x1b]112\a",                   // reset cursor color
				}, ""))
				_ = term.Tcsetattr(tty.TCSANOW, &state_before)
				term.Close()
			}()
		} else {
			defer term.Close()
		}
	}
	err = c.Run()
	if err != nil {
		fmt.Fprintln(os.Stderr, cmd[0], "failed with error:", err)
	}
}
