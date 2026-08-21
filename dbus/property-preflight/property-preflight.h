/*
 * property-preflight.h
 *
 * Generic machinery for pre-checking (and, where useful, quietly
 * normalizing, clamping, or deferring) an incoming D-Bus property
 * write before it reaches a gdbus-codegen generated skeleton's real
 * implementation. Interface-agnostic - no dependency on any specific
 * generated D-Bus header. Used by property-preflight-shairportsync.h
 * and property-preflight-mpris.h, and by any future interface set
 * built the same way.
 *
 *   - property_preflight_string_enum() / _normalize_string_enum():
 *     validate a "s"-typed property against a fixed set of acceptable
 *     values, either strictly (reject anything else) or leniently
 *     (accept a case-insensitive match, substituting the canonical
 *     spelling).
 *   - property_preflight_double_range() / _clamp_double_range(),
 *     property_preflight_int_range() / _clamp_int_range(): the same
 *     idea for a numeric property with a valid range, either strict
 *     or clamping.
 *   - PROPERTY_PREFLIGHT_DECLARE_SKELETON() / _DEFINE_SKELETON():
 *     generate a subclass of a given gdbus-codegen *Skeleton type
 *     whose D-Bus property writes are routed through an
 *     application-supplied validator function first.
 *
 * A validator can also silently drop a write (accept the D-Bus call,
 * but apply nothing) by setting *value to NULL - see the
 * PROPERTY_PREFLIGHT_DEFINE_SKELETON() doc comment below. This is the
 * basis of the "remote player" pattern used in
 * property-preflight-shairportsync.c and property-preflight-mpris.c:
 * for any property whose real value is only known once a remote
 * player (AirPlay source, hardware volume stage, etc.) confirms it,
 * the validator clamps/checks the requested value, fires off
 * whatever tells the remote player to change, and then drops the
 * write - the property itself is only ever actually updated later,
 * by whatever code already closes the loop when the remote player's
 * own confirmation event arrives.
 */

#ifndef __PROPERTY_PREFLIGHT_H__
#define __PROPERTY_PREFLIGHT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/* ========================================================================
 * Generic value-checking helpers
 * ======================================================================== */

/**
 * property_preflight_string_enum:
 * @property_name: the D-Bus property name, used only for the error message.
 * @value: the incoming value, expected to be a GVariant of type "s".
 * @valid_values: a NULL-terminated array of acceptable strings.
 * @interface: the D-Bus interface name, used only for the error message.
 * @error: return location for a #GError.
 *
 * Returns: %TRUE if @value's string is one of @valid_values; otherwise
 * %FALSE with @error set to a %G_DBUS_ERROR_INVALID_ARGS error listing
 * the acceptable values.
 */
gboolean property_preflight_string_enum(const gchar *property_name, GVariant *value,
                                        const gchar *const *valid_values, const gchar *interface,
                                        GError **error);

/**
 * property_preflight_normalize_string_enum:
 * @property_name: the D-Bus property name, used only for the error message.
 * @value: (inout): points to the incoming GVariant (type "s"). If its
 *   string is already an exact, case-sensitive match to one of
 *   @valid_values, @value is left untouched. If it matches one of
 *   @valid_values case-insensitively but not exactly, @value is
 *   replaced with a newly created GVariant holding the canonical
 *   spelling from @valid_values - the caller becomes responsible for
 *   unreffing this new variant once done with it (compare pointers
 *   with the original to tell whether a substitution happened).
 * @valid_values: the accepted values, spelled exactly as they should
 *   be stored (e.g. "Off", not "off").
 * @interface: the D-Bus interface name, used only for the error message.
 * @error: return location for a #GError.
 *
 * Returns: %TRUE if @value is (or has been normalized to) one of
 * @valid_values; otherwise %FALSE with @error set.
 */
gboolean property_preflight_normalize_string_enum(const gchar *property_name, GVariant **value,
                                                  const gchar *const *valid_values,
                                                  const gchar *interface, GError **error);

/**
 * property_preflight_double_range:
 * @property_name: the D-Bus property name, used only for the error message.
 * @value: the incoming value, expected to be a GVariant of type "d".
 * @min_value: the smallest acceptable value (inclusive).
 * @max_value: the largest acceptable value (inclusive).
 * @interface: the D-Bus interface name, used only for the error message.
 * @error: return location for a #GError.
 *
 * Returns: %TRUE if @value falls within [@min_value, @max_value];
 * otherwise %FALSE with @error set to a %G_DBUS_ERROR_INVALID_ARGS error.
 */
gboolean property_preflight_double_range(const gchar *property_name, GVariant *value,
                                         gdouble min_value, gdouble max_value,
                                         const gchar *interface, GError **error);

/**
 * property_preflight_clamp_double_range:
 * @property_name: unused, kept for signature symmetry with the check variant.
 * @value: (inout): points to the incoming GVariant (type "d"). If out
 *   of [@min_value, @max_value], @value is replaced with a newly
 *   created GVariant holding the clamped value - compare pointers
 *   with the original to tell whether a substitution happened.
 * @min_value: the smallest acceptable value (inclusive).
 * @max_value: the largest acceptable value (inclusive).
 * @error: unused - this never fails, any input can be clamped into range.
 *
 * Returns: always %TRUE.
 */
gboolean property_preflight_clamp_double_range(const gchar *property_name, GVariant **value,
                                               gdouble min_value, gdouble max_value,
                                               GError **error);

/**
 * property_preflight_int_range:
 * As property_preflight_double_range(), but for a GVariant of
 * type "i" (gint32).
 */
gboolean property_preflight_int_range(const gchar *property_name, GVariant *value, gint min_value,
                                      gint max_value, const gchar *interface, GError **error);

/**
 * property_preflight_clamp_int_range:
 * As property_preflight_clamp_double_range(), but for a GVariant of
 * type "i" (gint32).
 */
gboolean property_preflight_clamp_int_range(const gchar *property_name, GVariant **value,
                                            gint min_value, gint max_value, GError **error);

/* ========================================================================
 * Generic skeleton-wrapping machinery
 * ======================================================================== */

/**
 * PROPERTY_PREFLIGHT_DECLARE_SKELETON:
 * @TypeName: CamelCase type name for the new subclass, e.g. MyFooSkeleton.
 * @type_name: matching snake_case prefix, e.g. my_foo_skeleton.
 * @PublicType: the gdbus-codegen interface type to return from the
 *   constructor, e.g. ShairportSyncClient.
 *
 * Put this once per interface you want to wrap - see the six
 * invocations at the bottom of this file.
 */
#define PROPERTY_PREFLIGHT_DECLARE_SKELETON(TypeName, type_name, PublicType)                       \
  typedef struct _##TypeName TypeName;                                                             \
  typedef struct _##TypeName##Class TypeName##Class;                                               \
                                                                                                   \
  GType type_name##_get_type(void) G_GNUC_CONST;                                                   \
                                                                                                   \
  PublicType *type_name##_new(void);

/**
 * PROPERTY_PREFLIGHT_DEFINE_SKELETON:
 * @TypeName: must match the DECLARE_ invocation for this type.
 * @type_name: must match the DECLARE_ invocation for this type.
 * @ParentType: the gdbus-codegen generated Skeleton type being
 *   wrapped, e.g. ShairportSyncClientSkeleton.
 * @ParentTypeMacro: that type's GType macro, e.g.
 *   TYPE_SHAIRPORT_SYNC_CLIENT_SKELETON.
 * @PublicType: must match the DECLARE_ invocation for this type.
 * @PublicCastMacro: the cast macro for PublicType, e.g.
 *   SHAIRPORT_SYNC_CLIENT.
 * @ValidateFunc: a function (usually static) with the signature
 *
 *     gboolean ValidateFunc (const gchar  *property_name,
 *                            GVariant    **value,
 *                            GError      **error);
 *
 *   called for every incoming D-Bus property write on this interface.
 *   Return TRUE to accept the value (including for any property this
 *   validator doesn't care about), or FALSE with *error set to
 *   reject it - GDBusConnection turns that into a real D-Bus error
 *   reply, and the underlying GObject property is never touched.
 *
 *   *value initially points to the incoming (borrowed) GVariant.
 *   ValidateFunc may leave it untouched, or - to normalize/clean up
 *   an otherwise-valid input - replace *value with a newly created,
 *   owned GVariant (e.g. via g_variant_ref_sink (g_variant_new_string (...))).
 *   The wrapper takes care of unreffing a substituted value once it's
 *   been passed on to the generated setter; do not unref it yourself.
 *
 *   ValidateFunc may also set *value to NULL and return TRUE, to
 *   silently drop the write: the D-Bus caller sees a successful
 *   Properties.Set reply, but nothing is actually applied - the
 *   underlying GObject property, and therefore what a later Get or
 *   GetAll returns, is left completely untouched. This is the basis
 *   of the "remote player" pattern described at the top of this file.
 *
 *   If ValidateFunc substitutes a normalized/clamped value *and then*
 *   decides to drop it (setting *value to NULL afterwards), it is
 *   responsible for unreffing that intermediate substituted variant
 *   itself before overwriting *value - the wrapper's own cleanup
 *   never runs in the drop case, since it happens after the wrapper
 *   would have freed it.
 *
 * Put this in the matching .c section, after ValidateFunc's definition.
 */
#define PROPERTY_PREFLIGHT_DEFINE_SKELETON(TypeName, type_name, ParentType, ParentTypeMacro,       \
                                           PublicType, PublicCastMacro, ValidateFunc)              \
                                                                                                   \
  struct _##TypeName {                                                                             \
    ParentType parent_instance;                                                                    \
  };                                                                                               \
                                                                                                   \
  struct _##TypeName##Class {                                                                      \
    ParentType##Class parent_class;                                                                \
  };                                                                                               \
                                                                                                   \
  G_DEFINE_TYPE(TypeName, type_name, ParentTypeMacro)                                              \
                                                                                                   \
  static GDBusInterfaceSetPropertyFunc type_name##_original_set_property = NULL;                   \
                                                                                                   \
  static gboolean type_name##_set_property(GDBusConnection *connection, const gchar *sender,       \
                                           const gchar *object_path, const gchar *interface_name,  \
                                           const gchar *property_name, GVariant *value,            \
                                           GError **error, gpointer user_data) {                   \
    GVariant *effective_value = value;                                                             \
    gboolean result;                                                                               \
                                                                                                   \
    if (!ValidateFunc(property_name, &effective_value, error))                                     \
      return FALSE;                                                                                \
                                                                                                   \
    /* ValidateFunc can request a silent drop by setting *value to NULL:                           \
     * accept the D-Bus call (no error), but don't actually apply the                              \
     * write - the underlying GObject property is left untouched. */                               \
    if (effective_value == NULL)                                                                   \
      return TRUE;                                                                                 \
                                                                                                   \
    result = type_name##_original_set_property(connection, sender, object_path, interface_name,    \
                                               property_name, effective_value, error, user_data);  \
                                                                                                   \
    /* If ValidateFunc substituted a normalized/clamped value, it's ours                           \
     * to free - the original, borrowed 'value' is left alone. */                                  \
    if (effective_value != value)                                                                  \
      g_variant_unref(effective_value);                                                            \
                                                                                                   \
    return result;                                                                                 \
  }                                                                                                \
                                                                                                   \
  static GDBusInterfaceVTable *type_name##_get_vtable(GDBusInterfaceSkeleton *skeleton) {          \
    static GDBusInterfaceVTable my_vtable;                                                         \
    static gsize initialized = 0;                                                                  \
                                                                                                   \
    if (g_once_init_enter(&initialized)) {                                                         \
      const GDBusInterfaceVTable *generated_vtable =                                               \
          G_DBUS_INTERFACE_SKELETON_CLASS(type_name##_parent_class)->get_vtable(skeleton);         \
                                                                                                   \
      my_vtable = *generated_vtable;                                                               \
      type_name##_original_set_property = generated_vtable->set_property;                          \
      my_vtable.set_property = type_name##_set_property;                                           \
                                                                                                   \
      g_once_init_leave(&initialized, 1);                                                          \
    }                                                                                              \
                                                                                                   \
    return &my_vtable;                                                                             \
  }                                                                                                \
                                                                                                   \
  static void type_name##_class_init(TypeName##Class *klass) {                                     \
    GDBusInterfaceSkeletonClass *skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS(klass);          \
    skeleton_class->get_vtable = type_name##_get_vtable;                                           \
  }                                                                                                \
                                                                                                   \
  static void type_name##_init(TypeName *self) { (void)self; }                                     \
                                                                                                   \
  PublicType *type_name##_new(void) {                                                              \
    return PublicCastMacro(g_object_new(type_name##_get_type(), NULL));                            \
  }

G_END_DECLS

#endif /* __PROPERTY_PREFLIGHT_H__ */
