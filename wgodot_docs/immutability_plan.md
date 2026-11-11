# Immutability plan

Design, not implemented. This document describes the current data-class design.
Recommendations under remaining decisions are not yet agreed requirements.

## Scope and terminology

`@data_class` declares a class suitable for data storage and restricted construction.
`@immutable` qualifies a field, local, parameter or type usage. It protects ONE level
of access; it is not a declaration of a permanently/deeply immutable object graph.
The variable or field can be rebound to another appropriately qualified value.
Unqualified child references keep their declared mutability.

Keep mutable declarations as the default. Enforce contracts during analysis and
carry them into gd2cpp. Do not build a Rust borrow checker, freeze behavioral engine
objects, add a garbage collector, or add runtime checks to every element access.

## Data classes and eligible types

```gdscript
@data_class
class_name DamageDefinition
extends Resource

@immutable
var related: DamageDefinition

var actor: Node3D # Allowed: an ordinary mutable child reference.
var amount: int
```

A data class can inherit another data class or an explicitly approved native base.
Initial approved bases: Object, RefCounted, Resource. Node/Node2D/Node3D and similar
behavioral classes are not approved bases. Use an explicit classification, not a
heuristic based on names, size, whether a type is a reference, or observed mutations.
Approving a native base does not approve every behavioral subclass of that base.

Array, Dictionary and PackedXArray are intrinsic data-container types. They can
have qualified usages without adding a script annotation to the builtin types.
Ordinary script reference types must be declared `@data_class` to be eligible.

```gdscript
@immutable
var invalid_actor: Node3D # Error: behavioral type.

@immutable
var invalid_count: int   # Error: value type; annotation is unnecessary.

@immutable_return
func invalid_result() -> String: # Error: value-type result.
	return "text"
```

Value types, including int, float, bool, String, StringName and ordinary math values
such as Vector3/Color/Transform3D, do not use this qualification. String's COW value
semantics already preserve other copies when a local value changes. This does not
permit assigning a scalar member THROUGH an immutable-qualified data-class reference.
Handles to external state are not automatically data classes.

Behavioral classes and `@static_class` classes may contain immutable-qualified data
fields. The restriction concerns the annotated field's type, not the containing class.

## Variables, parameters and results

```gdscript
@immutable
var current := definition.energy

@immutable
var previous := current

current = another_definition.energy # Allowed; previous retains its old reference.
current[0] = 100                    # Error: mutates the referenced packed array.
current.append(100)                 # Error for the same reason.

var editable := current.duplicate() # Independent mutable packed array.
editable[0] = 100                   # Allowed.
```

An immutable-qualified field on a mutable holder can be rebound when the new value
has the required qualification. Qualification is not a final-binding rule. Writing
any direct member through an immutable-qualified holder is rejected outside its
initialization phases, even if the member's replacement is also immutable-qualified.

```gdscript
func calculate(
	@immutable damages: PackedInt32Array,
	working: PackedFloat32Array,
) -> int:
	return damages.size() + working.size()

@immutable_return
@readonly
func get_energy_values() -> PackedInt32Array:
	return self.energy # energy must have an @immutable declaration.
```

Immutable parameters accept immutable-qualified expressions; mutable parameters
accept mutable expressions. Qualified results need `@immutable_return`, which also
checks every return path. It is only valid for eligible data-class/container results.
It does not convert a mutable expression by fiat. Rebinding parameters is permitted
under the same rules as locals and does not rebind the caller's variable.

Parameter annotations need a proper parser target; current `parse_parameter()`
expects an identifier immediately. Propagate qualification through analyzed types,
expressions, signatures, overrides, interfaces, callable/lambda signatures, casts,
iteration, indexing and analysis caches. An AST flag on the first declaration alone
is insufficient. No cast or untyped intermediate may erase a qualification.

Fresh construction and verified independent copies may obtain the destination's
qualification. Thus a fresh packed-array `.duplicate()` can supply either kind of
parameter. An existing mutable alias cannot be silently reused as immutable:

```gdscript
var source := PackedInt32Array([1])
@immutable
var rejected := source             # Error: same mutable reference.
@immutable
var copied := source.duplicate()   # Allowed: independent packed array.
```

Copies retain nested qualifications. A shallow copy does not recursively clone or
freeze children. Native copy operations need accurate freshness contracts; do not
recognize them by the name "duplicate". User functions retain explicit return
contracts, and `@immutable_return` does not itself promise a fresh allocation.
Null retains ordinary typed-null behavior. Mixed mutable/immutable expressions
cannot silently discard qualification.

## Shallow containers and readonly methods

Use annotation syntax in type arguments: `Array[@immutable GameItem]`. Do not add
an `immutable` keyword or a second spelling. Each container level is independent:

```gdscript
var items: Array[int]
var game_items: Array[@immutable GameItem]

@immutable
var other_items: Array[OtherItem]

@readonly
func inspect_and_update_children() -> void:
	self.items.append(2)             # Allowed: items is a mutable child array.
	self.game_items.append(null)     # Allowed: the outer array is mutable.
	self.other_items.append(null)    # Error: the outer array is qualified immutable.
	self.other_items[0].item_id = 100 # Allowed: OtherItem usage is unqualified.
	self.items = []                  # Error: changes a direct member of self.
```

GameItem is a data class because its type usage is annotated. OtherItem may be a
normal unqualified class. `Array[Node3D]` is likewise a valid unqualified element
usage; it does not make Node3D an eligible immutable type.

To protect both the outer array and its elements:

```gdscript
@immutable
var game_items: Array[@immutable GameItem]

var by_id: Dictionary[int, @immutable GameItem]
```

Additional child fields inside GameItem need their own annotations. No automatic
transitive protection, rejection of mutable children, or hidden deep copy. A scalar
item_id cannot itself have `@immutable`; writes to it are blocked when the containing
GameItem reference is qualified, and allowed through an unqualified reference.
Rebinding a qualified data-valued member through a mutable holder still follows
normal qualified-assignment rules.

Reject container conversions that lose element qualifications. An alias from
`Array[@immutable GameItem]` to `Array[GameItem]` would let the caller insert a
mutable element into the same array. Indexing/iteration must preserve the declared
qualification of the element, not inherit the outer container's qualifier.

`@readonly` on a method protects its immediate receiver from direct writes and calls
to non-readonly self methods. It does not protect unqualified child references.
It is independent of `@immutable_return`, purity and compile-time evaluation.
Use annotations; no `const func` grammar or new keyword is planned.
Existing `@readonly` on variables keeps its existing assignment restriction.
Extend annotation target validation for methods without conflating the two meanings.

A readonly method may initialize independent results. It cannot write self's scalar
cache just because the field is private or named cache. It also must not manufacture
an immutable-return qualification from an otherwise mutable self reference merely
because the method is readonly. Declared immutable child fields can be returned with
`@immutable_return`; ordinary mutable children can be returned as mutable.

## Construction: C, A, B, D

`ClassName{ field: expression, ... }.new(arguments...)` is a statically checked field
initializer, not a Dictionary or an additional runtime object. The braces are
optional; `.new()` is mandatory. Apply the same syntax to mutable construction too.

```gdscript
@data_class
class_name Settings
extends Resource

var amount: int = 10
var label: String

@immutable
var child: OtherData

func _init(value: int = 20) -> void:
	self.amount = value

@immutable_return
@readonly
func duplicate_me() -> Settings:
	@immutable
	var result := Settings{
		amount: self.amount,
		child: self.child,
	}.new(self.amount)
	return result
```

The last method can return the construction expression directly. OtherData must be
a data class. No post-construction mutation window is inferred from later statements.

1. C: supplied brace fields replace their A initializers entirely. Do not run or
   allocate skipped defaults. Enforce ordinary field types and qualification.
2. A: initialize remaining fields using existing explicit/implicit defaults and
   base-before-derived, declaration-order behavior. Fields supplied by C are already
   available. Do not add dependency sorting or reset a C-supplied field first.
3. B: run `_init` and normal super constructor calls. B may overwrite fields through
   its construction-local permission. Keep ordinary argument/default behavior.
4. D: for resource loading, apply .tres/.tscn authored properties after initialization.
   Serialized fields are not C. They retain their existing final-override behavior.
   Binary resource loading and gd2cpp factories must produce the same result.

For `Settings{amount: 30}.new()`, A skips amount and B changes it to 20. A resource
with `amount = 40` then finishes at 40 after D. Missing serialized fields keep their
A/B values. The initialization machinery, not ordinary application code, performs D.

Keep normal expression evaluation. The current GDScript call rule evaluates arguments
before the receiver expression; gd2cpp already preserves it. Brace entries evaluate
in written order. Argument evaluation is separate from executing B's constructor body.
A `count = 10; doubled = self.count * 2` declaration gives doubled = 14 when C supplies
count = 7, otherwise 20. Existing forward-read and inheritance behavior remains.

C permits direct instance fields only. Reject accessor properties rather than invoking
or bypassing setters. Enforce existing `@private`, `@protected` and inherited access
rules at the call site; underscores alone impose no privacy. Unknown or duplicate
field entries are errors. No additional inheritance/super policy is needed for phases.

### Data-class construction cannot publish self

This restriction applies to data-class construction, including mutable construction.
When the destination is immutable-qualified, self carries that qualification but
its constructor body has permission to initialize its direct members.

```gdscript
func _init(registry: Registry) -> void:
	self.amount = 10                # Allowed in its constructor body.
	registry.remember(self)         # Error: publishes unfinished self.
	registry.saved = self           # Error for the same reason.
	self.verify_fields()            # Allowed only for a readonly, non-publishing call.
	super()                        # Normal base constructor, allowed.
```

Enforce the publication ban across initialization and helper calls, not just direct
arguments spelled self. A readonly helper that stores self in a registry still
publishes it; readonly only controls receiver mutation. For example, calling the
following helper from `_init` must fail:

```gdscript
@readonly
func register_in(registry: Registry) -> void:
	registry.saved = self # Does not write self, but exposes the unfinished instance.
```

An alias, returned self, captured callable/lambda, or initialization callback must not
bypass the same rule. Propagate known publication effects through analyzed calls;
unsupported native calls may not be treated as non-publishing guesses. This is
implementation of the agreed ban, not an extra permission question.

Once ordinary `.new()` or the resource loader finishes, behavioral caller code may
publish the result. Retain ordinary reference-counted ownership and failure cleanup.
No special cycle builder or rollback system is planned. Updating a completed data
instance through an immutable-qualified reference remains forbidden.

## Phase D allocation optimization

Goal: remove avoidable temporary allocations without modifying shared defaults.
This is an initialization optimization, not general permission to mutate through
immutable-qualified references when their reference count happens to be one.

For a small PackedInt32Array, the current source-mode loader allocates:

- The empty wrapper from automatic field initialization.
- The element buffer decoded from the file.
- A parsed Variant wrapper, which replaces and releases the empty field wrapper.

The parser already creates a temporary C++ Vector containing the parsed buffer
before it wraps that Vector in a Variant. Reuse this decoder and install its result
without constructing a temporary parsed wrapper when the destination can be reused.

During private resource initialization:

1. Parse into temporary buffer storage and validate successfully before changing
   the destination. This temporary Vector is not another heap-allocated wrapper.
2. If the existing automatically initialized field wrapper is exclusively owned,
   attach/swap the parsed Vector storage into that wrapper. Reuse the wrapper.
3. If the wrapper is shared, create the wrapper needed for the replacement and
   rebind only this field. Never replace storage inside the shared default wrapper.
4. If the property is absent, keep the initialized/default reference unchanged.

Use the existing wrapper reference count, not a new field on every array. Check it
through the owned member slot: retrieving a temporary Variant copy first would
itself add a reference. The check belongs at initialization/commit, not in every
read, element write or ordinary assignment. A reference count of one is not a lock;
limit this optimization to a loader-owned, unpublished initialization context.

Wrapper ownership and element-buffer ownership are separate. An exclusive wrapper
may still contain a COW buffer shared with another wrapper. Installing newly parsed
storage is safe; overwriting old buffer bytes requires the ordinary COW rules.
Do not implement this optimization by writing directly into a shared old buffer.

For an exclusive default and a singleton literal, this reduces the relevant wrapper/
buffer allocation events from three to two. It removes allocation churn, not the
final two allocations needed for a distinct nonempty packed value. Shared defaults
need a distinct replacement only when the serialized value actually differs/is set.
No timing or measured savings are claimed before implementation.

There is a second opportunity: GDScript currently constructs a default empty packed
wrapper even before an explicit initializer assigns a shared static value. Initialize
the field directly from that value where the compiler proves no earlier A initializer
reads it. Preserve normal typed-default/forward-read behavior elsewhere; do not skip
all typed initialization blindly. C-supplied defaults are already explicitly skipped.

The parser's existing AssignmentParser hook runs before generic Variant construction;
use shared decoding/initialization support, not duplicate numeric parsers or per-class
loader cases. Cover source text, binary caches and native factories with the same
observable initialization rules. GDScript Variant and gd2cpp typed member storage
have different allocation paths; verify them separately. Array/Dictionary require
appropriate typed-container handling, not assumptions based on packed-array layout.

## Short shared defaults in MyWuwa

Use static data fields for short repeated values, and resource data for bulk values.
No default_flags field or implicit broadcasting representation is planned.

```gdscript
@static_class
class_name CommonDataValues

@immutable
static var zero_int32_pack: PackedInt32Array = PackedInt32Array([0])
```

GDScript uses `PackedInt32Array([0])`; the text resource notation for the same value
is `PackedInt32Array(0)`. No new constructor overload is needed for that spelling.

```gdscript
class_name WorldCell
extends Resource

@immutable
@export var flags: PackedInt32Array = CommonDataValues.zero_int32_pack
```

WorldCell does not have to be a data class merely to contain an immutable packed
field. Mark it `@data_class` if WorldCell references themselves are to be qualified.
Use `@immutable` together with normal `@export` as shown, rather than requiring a
separate export-only mechanism. Source data/code organization still applies: do not
move large terrain arrays into generated GDScript constants.

The exporter should omit flags only when its complete value equals the actual stable
default. `[0]` is not equal to `[0, 0]`, and neither is equal to an empty array.
Current WorldCellBody indexes flags once per shape; retain those lengths and values.
Keep explicit arrays for differing values, with no runtime default_flags accessor.

Default omission tooling currently recognizes literal empty/zero defaults, not this
shared static declaration. Extend its supported metadata/resolution deliberately;
do not guess a value from the name zero_int32_pack or hash bulk arrays for comparison.
The remaining binding-stability decision below affects when omission is safe.

## Native integration and checking

Reuse native method constness metadata: binding templates set METHOD_FLAG_CONST,
GDScript already reads it, and builtin methods expose is_builtin_method_const().
This supports the shallow receiver contract. It does not mark return values or
behavioral objects immutable, and does not prohibit mutation of child objects.

Use explicit native data eligibility and argument/result contracts where signatures
do not establish them. A C++ mutable member is still a direct receiver mutation;
do not pretend const syntax proves that such a method obeys a stronger script
contract. The supported native base/builtin scope is deliberately limited. Do not
try to analyze or freeze the entire engine, or add method-name lists to gd2cpp.

Readonly methods must not expose mutable aliases to their protected receiver through
returns, casts or reflective APIs. Returning ordinary unqualified children stays
allowed by the shallow design. Dynamic/native boundaries must retain qualifications
or reject unsupported operations. Existing strict WGodot access rules remain.

## Remaining decisions

### Must subclasses retain the data-class restriction?

```gdscript
@data_class
class_name BaseData

# Separate class, deliberately missing @data_class:
class_name DerivedBehavior
extends BaseData
```

If DerivedBehavior can escape the construction rules, a BaseData-typed reference can
hide a behavioral implementation. Recommendation: every subclass must satisfy the
data-class contract. Decide whether the marker is inherited automatically or the
analyzer requires an explicit @data_class on each subclass. Do not silently allow
an unmarked subclass to weaken the base contract.

### Should shared defaults also have non-reassignable bindings?

`@immutable` permits rebinding. With only the annotation in CommonDataValues above,
this remains legal when the new expression is appropriately qualified:

```gdscript
CommonDataValues.zero_int32_pack = PackedInt32Array([7])
```

A later WorldCell whose flags assignment was omitted would then inherit `[7]`, even
though the exporter removed an original `[0]`. The array was never mutated; the
shared default's binding changed.

Recommendation: add the already-existing variable form of @readonly to shared
constants used for omission, while @immutable protects their contents:

```gdscript
@readonly
@immutable
static var zero_int32_pack: PackedInt32Array = PackedInt32Array([0])
```

This requires no new language feature. If intentional runtime default replacement
is desired instead, keep the binding reassignable and do not omit fixed serialized
values based on that default. Ordinary immutable-qualified variables remain
reassignable either way.

## Implementation sequence

1. Data-class eligibility and inheritance checks; immutable annotations on declarations,
   parameters, type arguments and returns; readonly method targets and body checks.
2. Propagate qualifiers and constructor publication restrictions through existing
   analysis, interfaces and caches. Implement C/A/B/D and matching gd2cpp generation.
3. Add safe resource initialization support and remove proven temporary allocations.
   Keep native bindings, loader references and ownership consistent across modes.
4. Migrate selected definition fields/readers and short shared defaults; extend the
   exporter to omit only proven stable defaults. Mutable gameplay state stays mutable.

Validate small complete examples for shallow access, nested container qualifiers,
reassignment, copying, constructor privacy/publication, C/A/B/D results, shared-default
preservation and resource-cache round trips. Compare interpreted/native behavior and
allocation counts without compiling the full worker merely to test syntax. No
permanent tests or engine build are requested by this planning document.
