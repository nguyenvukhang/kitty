// License: GPLv3 Copyright: 2023, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

type Set[T comparable] struct {
	items map[T]struct{}
}

func (self *Set[T]) Add(val T) {
	self.items[val] = struct{}{}
}

func (self *Set[T]) AddItems(val ...T) {
	for _, x := range val {
		self.items[x] = struct{}{}
	}
}

func (self *Set[T]) Has(val T) bool {
	_, ok := self.items[val]
	return ok
}

func NewSet[T comparable](capacity ...int) (ans *Set[T]) {
	if len(capacity) == 0 {
		ans = &Set[T]{items: make(map[T]struct{}, 8)}
	} else {
		ans = &Set[T]{items: make(map[T]struct{}, capacity[0])}
	}
	return
}
