// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package markup

import (
	"fmt"
	"strings"
	"unicode"

	"alatty/tools/utils"
	"alatty/tools/utils/style"
)

var _ = fmt.Print

type Context struct {
	fmt_ctx style.Context

	Cyan, Green, Blue, Magenta, Red, BrightRed, Yellow, Dim, Title, Exe, Opt, Emph, Err, Code func(args ...interface{}) string
	Url                                                                                                     func(string, string) string
}

var (
	fmt_ctx = style.Context{}
)

func New(allow_escape_codes bool) *Context {
	ans := Context{}
	ans.fmt_ctx.AllowEscapeCodes = allow_escape_codes
	fmt_ctx := &ans.fmt_ctx

	ans.Cyan = fmt_ctx.SprintFunc("fg=bright-cyan")
	ans.Red = fmt_ctx.SprintFunc("fg=red")
	ans.Magenta = fmt_ctx.SprintFunc("fg=magenta")
	ans.Green = fmt_ctx.SprintFunc("fg=green")
	ans.Blue = fmt_ctx.SprintFunc("fg=blue")
	ans.BrightRed = fmt_ctx.SprintFunc("fg=bright-red")
	ans.Yellow = fmt_ctx.SprintFunc("fg=bright-yellow")
	ans.Dim = fmt_ctx.SprintFunc("dim")
	ans.Title = fmt_ctx.SprintFunc("fg=blue")
	ans.Exe = fmt_ctx.SprintFunc("fg=bright-yellow")
	ans.Opt = ans.Green
	ans.Emph = ans.BrightRed
	ans.Err = fmt_ctx.SprintFunc("fg=bright-red")
	ans.Code = ans.Cyan
	ans.Url = fmt_ctx.UrlFunc("u=curly uc=cyan")

	return &ans
}

func Remove_backslash_escapes(text string) string {
	// see https://docutils.sourceforge.io/docs/ref/rst/restructuredtext.html#escaping-mechanism
	out := strings.Builder{}
	prev_was_slash := false
	out.Grow(len(text))
	for _, ch := range text {
		if prev_was_slash {
			if !unicode.IsSpace(ch) {
				out.WriteRune(ch)
			}
			prev_was_slash = false
		} else {
			if ch == '\\' {
				prev_was_slash = true
			} else {
				out.WriteRune(ch)
			}
		}
	}
	return out.String()
}

func ReplaceAllRSTRoles(str string, repl func(Rst_format_match) string) string {
	var m Rst_format_match
	rf := func(full_match string, groupdict map[string]utils.SubMatch) string {
		m.Payload = groupdict["payload"].Text
		m.Role = groupdict["role"].Text
		return repl(m)
	}
	return utils.ReplaceAll(utils.MustCompile(":(?P<role>[a-z]+):(?:(?:`(?P<payload>[^`]+)`)|(?:'(?P<payload>[^']+)'))"), str, rf)
}

func (self *Context) hyperlink_for_url(url string, text string) string {
	return self.Url(url, text)
}

func Text_and_target(x string) (text string, target string) {
	parts := strings.SplitN(x, "<", 2)
	text = strings.TrimSpace(parts[0])
	target = strings.TrimRight(parts[len(parts)-1], ">")
	return
}

type Rst_format_match struct {
	Role, Payload string
}

func (self *Context) link(x string) string {
	text, url := Text_and_target(x)
	return self.hyperlink_for_url(url, text)
}

func (self *Context) ref_hyperlink(x string, prefix string) string {
	text, target := Text_and_target(x)
	url := "alatty+doc://" + utils.Hostname() + "/#ref=" + prefix + target
	text = ReplaceAllRSTRoles(text, func(group Rst_format_match) string {
		return group.Payload
	})
	return self.hyperlink_for_url(url, text)
}

func (self *Context) Prettify(text string) string {
	return ReplaceAllRSTRoles(text, func(group Rst_format_match) string {
		val := group.Payload
		switch group.Role {
		case "env", "envvar":
			return self.ref_hyperlink(val, "envvar-")
		case "iss":
			return self.ref_hyperlink(val, "issues-")
		case "pull":
			return self.ref_hyperlink(val, "pull-")
		case "disc":
			return self.ref_hyperlink(val, "discussions-")
		case "ref":
			return self.ref_hyperlink(val, "")
		case "ac":
			return self.ref_hyperlink(val, "action-")
		case "term":
			return self.ref_hyperlink(val, "term-")
		case "code":
			return self.Code(Remove_backslash_escapes(val))
		case "link":
			return self.link(val)
		case "option":
			idx := strings.LastIndex(val, "--")
			if idx < 0 {
				idx = strings.Index(val, "-")
			}
			if idx > -1 {
				val = strings.TrimSuffix(val[idx:], ">")
			}
			return val
		case "opt":
			return val
		case "yellow":
			return self.Yellow(val)
		case "blue":
			return self.Blue(val)
		case "green":
			return self.Green(val)
		case "cyan":
			return self.Cyan(val)
		case "magenta":
			return self.Magenta(val)
		default:
			return val
		}

	})
}

func (self *Context) SetAllowEscapeCodes(allow_escape_codes bool) {
	self.fmt_ctx.AllowEscapeCodes = allow_escape_codes
}

func (self *Context) EscapeCodesAllowed() bool {
	return self.fmt_ctx.AllowEscapeCodes
}
