{
  config,
  lib,
  ...
}:
let
  cfg = config.programs.nythera;
in
{
  # The module from this flake predates the module in nixpkgs.
  # To avoid conflicts, we disable the nixpkgs module.
  disabledModules = [ "programs/wayland/nythera.nix" ];

  options.programs.nythera = {
    enable = lib.mkEnableOption "Whether to enable nythera, a lightweight Wayland shell and bar.";

    package = lib.mkOption {
      type = lib.types.nullOr lib.types.package;
      default = null;
      description = "The nythera package to install.";
    };

    systemd = {
      enable = lib.mkEnableOption "Enables a systemd user service for nythera.";

      target = lib.mkOption {
        type = lib.types.str;
        default = "graphical-session.target";
        example = "hyprland-session.target";
        description = "The systemd user target for the nythera service.";
      };
    };

    recommendedServices.enable = lib.mkEnableOption ''
      NixOS services used by Nythera v5 integrations, including NetworkManager,
      Bluetooth, UPower, and a power profile service.
    '';
  };

  config = lib.mkIf cfg.enable (
    lib.mkMerge [
      {
        hardware.graphics.enable = lib.mkDefault true;

        environment.systemPackages = lib.optional (cfg.package != null) cfg.package;

        systemd.user.services.nythera = lib.mkIf cfg.systemd.enable {
          description = "Nythera - A lightweight Wayland shell and bar";
          documentation = [ "https://docs.noctalia.dev/noctalia/" ];
          partOf = [ cfg.systemd.target ];
          after = [ cfg.systemd.target ];
          wantedBy = [ cfg.systemd.target ];
          restartTriggers = [ cfg.package ];

          environment.PATH = lib.mkForce null;

          serviceConfig = {
            ExecStart = lib.getExe cfg.package;
            Restart = "on-failure";
          };
        };

        assertions = [
          {
            assertion = !cfg.systemd.enable || cfg.package != null;
            message = "programs.nythera.package cannot be null when programs.nythera.systemd.enable is true";
          }
        ];
      }

      (lib.mkIf cfg.recommendedServices.enable {
        networking.networkmanager.enable = lib.mkDefault true;
        hardware.bluetooth.enable = lib.mkDefault true;
        services.upower.enable = lib.mkDefault true;
        services.power-profiles-daemon.enable = lib.mkIf (!config.services.tuned.enable) (
          lib.mkDefault true
        );
      })
    ]
  );
}
