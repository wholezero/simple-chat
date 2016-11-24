@0xfa85696300c89e75;

using Spk = import "/sandstorm/package.capnp";

const pkgdef :Spk.PackageDefinition = (
  id = "yp9xt15y8s0rsuntnhgnr510qpcvm2tatd9mn1gzwr2au8h2n4p0",
  manifest = (
    appTitle = (defaultText = "Simple Chat"),
    appVersion = 12,  # Increment this for every release.
    appMarketingVersion = (defaultText = "0.6.0"),

    actions = [
      ( nounPhrase = (defaultText = "logged room"),
        command = (
          argv = ["/serve", "-i"],
          environ = []
        )
      ),
      ( nounPhrase = (defaultText = "off-the-record room"),
        command = (
          argv = ["/serve"],
          environ = []
        )
      )
    ],
    continueCommand = (
      argv = ["/serve"],
      environ = []
    ),

    metadata = (
      icons = (
        appGrid = (svg = embed "icon.svg"),
        #grain = (svg = embed "path/to/grain-24x24.svg"),
        #market = (svg = embed "path/to/market-150x150.svg"),
        #marketBig = (svg = embed "path/to/market-big-300x300.svg"),
      ),

      website = "https://github.com/wholezero/simple-chat",
      codeUrl = "https://github.com/wholezero/simple-chat",
      license = (none = void),

      categories = [communications, productivity],

      author = (
        contactEmail = "i@wholezero.org",
        pgpSignature = embed "pgp-signature",
      ),
      pgpKeyring = embed "pgp-keyring",

      description = (defaultText = embed "description.md"),
      shortDescription = (defaultText = "Really simple chat"),

      screenshots = [
        (width = 344, height = 527, png = embed "shot.png"),
        #(width = 746, height = 795, jpeg = embed "path/to/screenshot-1.jpeg"),
        #(width = 640, height = 480, png = embed "path/to/screenshot-2.png"),
      ],
      changeLog = (defaultText = embed "changelog.md"),
    ),
  ),

  sourceMap = (
    searchPath = [
      ( sourcePath = "target" ),  # Search this directory first.
      ( sourcePath = "/",    # Then search the system root directory.
        hidePaths = [ "home", "proc", "sys",
                      "etc/passwd", "etc/hosts", "etc/host.conf",
                      "etc/nsswitch.conf", "etc/resolv.conf" ]
      )
    ]
  ),

  fileList = "sandstorm-files.list",

  alwaysInclude = [],
  # Fill this list with more names of files or directories that should be
  # included in your package, even if not listed in sandstorm-files.list.
  # Use this to force-include stuff that you know you need but which may
  # not have been detected as a dependency during `spk dev`. If you list
  # a directory here, its entire contents will be included recursively.

  #bridgeConfig = (
  #  # Used for integrating permissions and roles into the Sandstorm shell
  #  # and for sandstorm-http-bridge to pass to your app.
  #  # Uncomment this block and adjust the permissions and roles to make
  #  # sense for your app.
  #  # For more information, see high-level documentation at
  #  # https://docs.sandstorm.io/en/latest/developing/auth/
  #  # and advanced details in the "BridgeConfig" section of
  #  # https://github.com/sandstorm-io/sandstorm/blob/master/src/sandstorm/package.capnp
  #  viewInfo = (
  #    # For details on the viewInfo field, consult "ViewInfo" in
  #    # https://github.com/sandstorm-io/sandstorm/blob/master/src/sandstorm/grain.capnp
  #
  #    permissions = [
  #    # Permissions which a user may or may not possess.  A user's current
  #    # permissions are passed to the app as a comma-separated list of `name`
  #    # fields in the X-Sandstorm-Permissions header with each request.
  #    #
  #    # IMPORTANT: only ever append to this list!  Reordering or removing fields
  #    # will change behavior and permissions for existing grains!  To deprecate a
  #    # permission, or for more information, see "PermissionDef" in
  #    # https://github.com/sandstorm-io/sandstorm/blob/master/src/sandstorm/grain.capnp
  #      (
  #        name = "editor",
  #        # Name of the permission, used as an identifier for the permission in cases where string
  #        # names are preferred.  Used in sandstorm-http-bridge's X-Sandstorm-Permissions HTTP header.
  #
  #        title = (defaultText = "editor"),
  #        # Display name of the permission, e.g. to display in a checklist of permissions
  #        # that may be assigned when sharing.
  #
  #        description = (defaultText = "grants ability to modify data"),
  #        # Prose describing what this role means, suitable for a tool tip or similar help text.
  #      ),
  #    ],
  #    roles = [
  #      # Roles are logical collections of permissions.  For instance, your app may have
  #      # a "viewer" role and an "editor" role
  #      (
  #        title = (defaultText = "editor"),
  #        # Name of the role.  Shown in the Sandstorm UI to indicate which users have which roles.
  #
  #        permissions  = [true],
  #        # An array indicating which permissions this role carries.
  #        # It should be the same length as the permissions array in
  #        # viewInfo, and the order of the lists must match.
  #
  #        verbPhrase = (defaultText = "can make changes to the document"),
  #        # Brief explanatory text to show in the sharing UI indicating
  #        # what a user assigned this role will be able to do with the grain.
  #
  #        description = (defaultText = "editors may view all site data and change settings."),
  #        # Prose describing what this role means, suitable for a tool tip or similar help text.
  #      ),
  #      (
  #        title = (defaultText = "viewer"),
  #        permissions  = [false],
  #        verbPhrase = (defaultText = "can view the document"),
  #        description = (defaultText = "viewers may view what other users have written."),
  #      ),
  #    ],
  #  ),
  #  #apiPath = "/api",
  #  # Apps can export an API to the world.  The API is to be used primarily by Javascript
  #  # code and native apps, so it can't serve out regular HTML to browsers.  If a request
  #  # comes in to your app's API, sandstorm-http-bridge will prefix the request's path with
  #  # this string, if specified.
  #),
);
