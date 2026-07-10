package com.fyber.mediation;

import java.lang.Object;
import java.lang.String;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public final class MediationConfigProvider {
  public static Map<String, Map<String, Object>> getRuntimeConfigs() {
    return Collections.emptyMap();
  }

  public static Map<String, Map<String, Object>> getConfigs() {
    return new HashMap<>();
  }
}
