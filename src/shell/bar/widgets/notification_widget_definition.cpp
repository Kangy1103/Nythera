#include "shell/bar/widgets/notification_widget_definition.h"

const nythera::bar::WidgetDefinition<NotificationWidget::Options>& notificationWidgetDefinition() {
  using nythera::bar::field;
  using Options = NotificationWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "notifications",
      .fields = {
          field<&Options::hideWhenNoUnread>({
              .key = "hide_when_no_unread",
          }),
      },
  };
  return definition;
}
