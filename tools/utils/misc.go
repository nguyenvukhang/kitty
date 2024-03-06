// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"fmt"

	"golang.org/x/exp/slices"
)

var _ = fmt.Print

func Reverse[T any](s []T) []T {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
	return s
}

func Sort[T any](s []T, cmp func(a, b T) int) []T {
	slices.SortFunc(s, cmp)
	return s
}

func StableSort[T any](s []T, cmp func(a, b T) int) []T {
	slices.SortStableFunc(s, cmp)
	return s
}

func ShiftLeft[T any](s []T, amt int) []T {
	leftover := len(s) - amt
	if leftover > 0 {
		copy(s, s[amt:])
	}
	return s[:leftover]
}
