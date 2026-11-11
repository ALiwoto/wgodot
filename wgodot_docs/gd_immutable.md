# GDScript immutability

WGodot checks immutability during analysis, including `wg check` and gd2cpp.
Qualifiers belong to references in the AST; generated objects and resource payloads
carry no immutability flags. This is a shallow contract, not a runtime freeze or a
guarantee that an entire object graph is safe for concurrent mutation.

## Qualifiers

| Annotation | Contract |
| --- | --- |
| `@data_class` | Restricted data-object construction; inherited by every subclass. |
| `@immutable` | Cannot mutate the referenced object/container through this reference. The binding may be reassigned. |
| `@readonly` on a variable | Cannot reassign the binding under the existing readonly-variable rules. Does not freeze the referenced object. |
| `@readonly` on a method | Cannot change direct receiver state, call mutating receiver methods, or publish/alias the protected receiver. |
| `@immutable_return` | Returns an immutable reference; receiving declarations must retain that qualifier. |
| `@immutable_clone` | Returns a fresh, unpublished instance of its own data class; the caller can choose mutable or immutable. |

Use `@immutable` on Array, Dictionary, PackedXArray, data-class instances, and native
Resources. Value types such as int, String and Vector3 reject it. Nodes also reject
it. A behavioral class may contain immutable data fields without being a data class.
Data classes inherit another data class or exactly Object, RefCounted or Resource;
an ordinary behavioral script base is ineligible.

```gdscript
@data_class
class_name ItemData
extends Resource

@export var amount: int
@immutable
@export var weights: PackedInt32Array
var actor: Node3D # An ordinary mutable child reference is allowed.

@readonly
func total(@immutable other: ItemData) -> int:
	return self.amount + other.amount

@override
@readonly
func _to_string() -> String:
	return "ItemData(%d)" % self.amount
```

Every script `_to_string()` override must explicitly have `@readonly`, including
overrides on behavioral classes. Implicit conversion through `str`/printing can
invoke it. The body receives the same shallow checks as other readonly methods.

```gdscript
@immutable
var first := ItemData{amount: 10}.new()
@immutable
var current := first
current = ItemData{amount: 20}.new() # Valid: replaces the reference.
current.amount = 30                # Error: writes through an immutable receiver.
current.weights.append(2)          # Error: weights itself is immutable.
var leaked := current             # Error: loses qualification.
```

An existing mutable reference cannot silently become immutable. Fresh construction
and verified independent copies can supply either kind of destination. Ordinary
typed null remains allowed. Casts, assignments, calls and container conversions must
retain qualifiers. User functions need explicit return contracts.

## Shallow access and containers

Each reference level has its own declaration:

```gdscript
var items: Array[@immutable ItemData] = [] # Mutable array, immutable elements.
@immutable
var fixed_items: Array[ItemData] = []     # Immutable array, mutable elements.
@immutable
var definitions: Array[@immutable ItemData] = [] # Both levels protected.
var by_id: Dictionary[int, @immutable ItemData] = {}
```

Reading a child retains the child's declared qualifier. For example, a readonly
method can append to an unqualified child array, but cannot replace that member.
Likewise an immutable ItemData can expose its ordinary mutable `actor` reference.
Qualify child references explicitly when they also need protection. There is no
implicit deep copy or transitive freeze.

Parameters use `func consume(@immutable value: ItemData) -> void:`. Immutable
parameters accept qualified references or fresh values; mutable parameters reject
immutable aliases. Packed-array `.duplicate()` makes an independent mutable wrapper
with ordinary copy-on-write element storage. Shallow copies preserve nested qualifiers.

## Results and clones

Methods inside ItemData can return shared data or a new instance:

```gdscript
@immutable_return
@readonly
func shared_weights() -> PackedInt32Array:
	return self.weights

@immutable_clone
@readonly
func clone() -> ItemData:
	return ItemData{amount: self.amount, weights: self.weights}.new()
```

```gdscript
@immutable
var weights := current.shared_weights() # Required qualifier.
var editable := current.clone()         # Mutable result is allowed.
@immutable
var another := current.clone()          # Immutable result is also allowed.
```

`@immutable_return` does not promise a copy. `@immutable_clone` must return fresh
construction, another verified clone, or an `@immutable` local initialized and
reassigned exclusively from those expressions. Returning self or an existing shared
instance is rejected. Clone methods cannot publish instances of their result class
through arguments, fields, containers or captures. The child fields still follow
their own qualifiers; the example intentionally shares immutable weights.

Readonly and result annotations are independent. A non-readonly clone method cannot
be called through an immutable receiver.

## Construction: C/A/B/D

`Type{field: expression, ...}.new(arguments...)` initializes direct instance fields.
Braces are optional; `.new()` is required. This also works for mutable construction.

1. **C:** supplied fields replace their A defaults entirely, including side effects
   and allocations. Entries evaluate in written order; normal call arguments are
   evaluated before the receiver/brace expressions.
2. **A:** initialize remaining fields in normal base-before-derived/declaration order.
   C-supplied fields are already available to these initializers.
3. **B:** run `_init` and ordinary super-constructor behavior. It may overwrite C/A.
4. **D:** resource loading applies authored `.tres`/`.tscn` properties after B.
   Missing properties retain their A/B values. Text, binary and native loaders use
   this ordering; serialized properties are not C initializers.

For a field with A default 10, a C value of 30 and a B assignment of 20, construction
finishes at 20. A subsequent D assignment of 40 makes the loaded value 40. If another
A initializer reads that field, it sees C's 30 before B runs.

C rejects unknown/duplicate fields, static fields and accessor properties. Existing
private/protected access rules apply. A data-class initializer, constructor or setter
cannot publish self, alias it, capture it in a callback, or pass it elsewhere.
Self helpers must be readonly and non-publishing; unaudited native self calls are
rejected. Ordinary callers can publish the completed result.

## Loading and sharing

Resource results from `load`, `preload`, `async_preload` and native ResourceLoader
APIs require immutable references. Script resource types must be data classes;
loading a script class itself remains a class/metatype operation.

```gdscript
const ITEM: WResPath = "res://items/example.tres"
@immutable
var definition := load(ITEM) as ItemData
```

Normal cache reuse can return the same object to multiple callers. Native
Resource-valued properties/getters also expose immutable references; their setters
accept immutable references or fresh values. Native methods called through immutable
references must be declared const. Nodes stay mutable; instantiating a loaded
PackedScene produces ordinary mutable Nodes. Cache replacement modes that mutate
published resources are rejected by the analyzer.

For short shared defaults:

```gdscript
@static_class
class_name CommonDataValues

@immutable
@readonly
static var zero_int32_pack: PackedInt32Array = PackedInt32Array([0])
```

```gdscript
# Inside a data class:
@immutable
@export var flags: PackedInt32Array = CommonDataValues.zero_int32_pack
```

Assignments share the packed-array wrapper and buffer. Packed literals construct
directly without a temporary GDScript Array. The compiler skips redundant empty
initialization where forward-read behavior permits; immutable empty defaults can
also be shared. During private loading, packed decoding reuses an exclusively owned
wrapper and replaces a shared wrapper without modifying its old storage.

Keep bulk data in typed resources. `.tres` references such as
`grid_y = GlobalResource("terrain_defaults").grid_127_0_126` share the global member;
native resource payloads preserve that link using resource/field IDs. Omitted fields
retain shared defaults. Exporters may omit a value only when the complete value
equals a proven stable default: `[0]`, `[0, 0]` and `[]` are distinct.

## Explicit unsafe access and current boundary

```gdscript
var editable := dangerous_return_as_mutable(ItemData, definition)
editable.amount = 99 # Changes the same object seen by every alias.
```

This removes the outer qualifier without copying, unlocking storage or changing the
object's type. Nested qualifiers remain. The caller owns the consequences for other
readers and threads; use a verified clone when an independent object is wanted.

Known unresolved boundary: `instance_from_id(value.get_instance_id()) as ItemData`
can recover an unqualified alias. It is currently accepted, including by gd2cpp;
do not use it to bypass these contracts. Its restriction is pending a policy decision.
