#pragma once

#include <utility>
#include <vector>

#include "esphome/core/automation.h"
#include "light_effect.h"

namespace esphome {
namespace light {

class FlameLightEffect : public LightEffect {
 public:
  explicit FlameLightEffect(const std::string &name) : LightEffect(name) {}

  void start() override {
      this->is_baseline_brightness_needed_ = true;
      this->flickers_left_ = 0;
      if (!this->have_custom_flicker_intensity_) {
        this->flicker_intensity_ = this->intensity_ / number_levels_ / 2.0f;
        ESP_LOGD("FlameLightEffect", "Calculated flicker intensity: %.3f    Overall intensity: %.3f",
          this->flicker_intensity_, this->intensity_);
      }
  }

  void stop() override {
      ESP_LOGD("FlameLightEffect", "Restore Initial Brightness: %.3f",
        this->initial_brightness_);
      auto call = this->state_->make_call();
      call.set_color_mode(this->color_mode_);
      call.set_brightness(this->initial_brightness_);
      call.perform();
  }

  void apply() override {

    if (this->state_->is_transformer_active()) {
      // Something is already running.
      return;
    }

    if (this->have_custom_color_){
      // This is not in the start() because we do not want to
      // interrupt a running transformer.
      this->have_custom_color_ = false; // Only do this once.

      ESP_LOGD("FlameLightEffect", "Have a custom color  R: %.2f  G: %.2f  B: %.2f",
        this->red_, this->green_, this->blue_);

      auto call = this->state_->make_call();
      call.set_color_mode(ColorMode::RGB);
      // Commented out 'cause We will use the default transistion.
      // call.set_transition_length(transition_time_ms * 4);
      call.set_brightness(1.0f);
      call.set_rgb(this->red_, this->green_, this->blue_);
      call.set_state(true);
      call.perform();

      return;
    }

    if (this->is_baseline_brightness_needed_){
      // Only get the brightness after all transitions have finished otherwise
      // we may get zero/off.
      this->is_baseline_brightness_needed_ = false;
      this->initial_brightness_ = this->baseline_brightness_ = this->state_->current_values.get_brightness();
      this->color_mode_ = this->state_->current_values.get_color_mode();

      ESP_LOGD("FlameLightEffect", "Initial Brightness: %.3f   Intensity: %.3f   Flicker Intensity: %.3f",
        this->baseline_brightness_, this->sub_intensity_, this->flicker_intensity_);

      ESP_LOGD("FlameLightEffect", "Speed: %dms   Jitter: %d    Flicker Percent: %.3f",
        this->transition_length_ms_, this->transition_length_jitter_ms_, this->flicker_probability_);

      if (this->is_baseline_brightness_dim_){
        // Special case to ensure our max brightness increase can be accommodated.
        // Remember, sub_intensity_ is 1/3 our overall intensity swing specified by the config.
        // We guard-band a little to ensure we never reach 100%.
        if (this->baseline_brightness_ >= 1.0f - this->intensity_)
        {
          this->baseline_brightness_ = 1.0f - this->intensity_;
          ESP_LOGD("FlameLightEffect", "Adjusting the baseline brightness to %.3f", this->baseline_brightness_);
          auto call = this->state_->make_call();
          call.set_brightness(this->baseline_brightness_);
          call.set_state(true);
          call.perform();
        }
      }

      set_min_max_brightness();
    }
  }

  /*
   * The overall max intensity swing of the flicker, based on the starting brightness.
   */
  void set_intensity(float intensity) { this->intensity_ = intensity; this->sub_intensity_ = intensity / 3.0f; }
  void set_flicker_intensity(float flicker_intensity) { this->flicker_intensity_ = flicker_intensity; this->have_custom_flicker_intensity_ = true; }
  void set_flicker_probability(float percent) { this->flicker_probability_ = percent; }
  void set_flicker_transition_length(int speed_ms) { this->transition_length_ms_ = speed_ms; }
  void set_flicker_transition_length_jitter(int speed_jitter_ms) { this->transition_length_jitter_ms_ = speed_jitter_ms; }
  void set_red(float red) { this->red_ = red; if(red > 0.0f){ this->have_custom_color_ = true; }}
  void set_green(float green) { this->green_ = green;  if(green > 0.0f){ this->have_custom_color_ = true; }}
  void set_blue(float blue) { this->blue_ = blue;  if(blue > 0.0f){ this->have_custom_color_ = true; }}
  void set_red2(float red) { this->red2_ = red; if(red > 0.0f){ this->have_custom_color2_ = true; }}
  void set_green2(float green) { this->green2_ = green;  if(green > 0.0f){ this->have_custom_color2_ = true; }}
  void set_blue2(float blue) { this->blue2_ = blue;  if(blue > 0.0f){ this->have_custom_color2_ = true; }}

 protected:
  float intensity_ = 0.10f;
  // The intensity of each step, one third of the requested intensity since we have
  // three steps below max brightness.
  float sub_intensity_ = 0.10f;
  float flicker_probability_ = 0.8f;
  bool have_custom_flicker_intensity_ = false;
  float flicker_intensity_ = 0.50f;
  int transition_length_ms_ = 100;
  int transition_length_jitter_ms_ = 10;

  bool have_custom_color_ = false;
  float red_ = 0.0f;
  float green_ = 0.0f;
  float blue_ = 0.0f;

  bool have_custom_color2_ = false;
  float red2_ = 0.0f;
  float green2_ = 0.0f;
  float blue2_ = 0.0f;

  bool is_baseline_brightness_dim_ = false;

  // State
  bool is_baseline_brightness_needed_;
  float initial_brightness_;
  float baseline_brightness_;
  ColorMode color_mode_ = ColorMode::UNKNOWN;
  int flicker_state_ = 0;
  int previous_flicker_state_ = 0;
  int flickers_left_ = 0;

  /*
   * Track if the flicker is on the bright or dim part of the
   * cycle so it can be toggled.
   */
  bool is_in_bright_flicker_state_ = true;

  /*
   * The brightness percent for the bright part of the flicker.
   * This gets changes in the code when a new flicker state is determined
   * and is scaled based on many factors.
   */
  float flicker_bright_brightness_ = 0.95f;

  /*
   * The brightness percent for the dim part of the flicker.
   * This gets changes in the code when a new flicker state is determined
   * and is scaled based on many factors.
   */
  float flicker_dim_brightness_ = 0.90f;

  //float brightness_scale_ = 1.0f;

  const float number_levels_ = 3.0f;

  float max_brightness_ = 1.0f;
  float min_brightness_ = 0.0f;
  /*
   * Called after the baseline brightness is found. The
   * method should set max_brightness_ and min_brightness_
   * which are in turn used to determine the color of light
   * in a two-color mode based on the brightness.
   */
  virtual void set_min_max_brightness() = 0;
};

/*
 * A candle has a normal brightness and flickers dim when a breese comes by.
 */
class CandleLightEffect : public FlameLightEffect {
 public:
  explicit CandleLightEffect(const std::string &name) : FlameLightEffect(name) {}

  void apply() override {

      FlameLightEffect::apply();

      if (this->state_->is_transformer_active()) {
        // Something is already running.
        return;
      }


      float new_brightness;
      int transition_time_ms;

      if (this->flickers_left_ > 0) {
        this->flickers_left_ -= 1;

        float r = random_float();
        if (r <= 0.5){
          transition_time_ms = this->transition_length_ms_;
        } else {
          transition_time_ms = this->transition_length_ms_ + this->transition_length_jitter_ms_;
        }

        if (is_in_bright_flicker_state_) {
          new_brightness = this->flicker_dim_brightness_;
          is_in_bright_flicker_state_ = false;
        } else {
          new_brightness = this->flicker_bright_brightness_;
          is_in_bright_flicker_state_ = true;
        }
      } else {
        float r = random_float();
        float dim_depth = 1.0f;
        // Floating point multiplication is faster than division.
        if (r <= this->flicker_probability_ * 0.10f) {
          dim_depth = 2.0f;
          this->flicker_state_ = 3;
        } else if (r <= this->flicker_probability_ * 0.5f) {
          dim_depth = 1.0f;
          this->flicker_state_ = 2;
        } else if (r <= this->flicker_probability_) {
          dim_depth = 0.0f;
          this->flicker_state_ = 1;
        } else {
          dim_depth = 0.0f;
          this->flicker_state_ = 0;
        }

        if(this->flicker_state_ == 0){
          // No flicker.
          this->flicker_bright_brightness_ = this->baseline_brightness_;
          this->flicker_dim_brightness_ = this->baseline_brightness_;
        } else {
          // Scale how dim it goes based on the initial brightness.
          // E.g. if the step flicker intensity is 10% and we are at 50%, then only
          // go down 5%.
          this->flicker_bright_brightness_ = this->baseline_brightness_ - ( dim_depth * this->sub_intensity_ * this->baseline_brightness_);
          this->flicker_dim_brightness_ = flicker_bright_brightness_ - (this->sub_intensity_ * this->flicker_intensity_ * this->baseline_brightness_);
        }

        if( this->flicker_bright_brightness_ > 1.0f ){ this->flicker_bright_brightness_ = 1.0f; }
        else if( this->flicker_bright_brightness_ < 0.0f ){ this->flicker_bright_brightness_ = 0.0f; }

        if( this->flicker_dim_brightness_ > 1.0f ){ this->flicker_dim_brightness_ = 1.0f; }
        else if( this->flicker_dim_brightness_ < 0.0f ){ this->flicker_dim_brightness_ = 0.0f; }

        // Determine how many flickers should be done for the new flicker_state.
        r = random_float();
        if (this->flicker_state_ == 3) {
          this->flickers_left_ = 1; // Always only one for the darkest flicker state.
        } else if ( r <= 0.2 ) {
          this->flickers_left_ = 10; // 5 bright 5 dim
        } else if ( r <= 0.6 ) {
          this->flickers_left_ = 8;
        } else if ( r <= 0.7 ) {
          this->flickers_left_ = 4;
        } else {
          this->flickers_left_ = 2;
        }

        int delta = abs(this->flicker_state_ - this->previous_flicker_state_);
        r = random_float();
        if (r <= 0.5){
          transition_time_ms = this->transition_length_ms_ * (delta < 1 ? 1 : delta);
        } else {
          transition_time_ms = (this->transition_length_ms_ + this->transition_length_jitter_ms_) * (delta < 1 ? 1 : delta);
        }

        r = random_float();
        if (r <= 0.5){
          is_in_bright_flicker_state_ = true; // start with the brighter of the two.
        } else {
          is_in_bright_flicker_state_ = false; // start with the dimmer of the two.
        }

        new_brightness = flicker_bright_brightness_;
        this->previous_flicker_state_ = this->flicker_state_;
      }

      if(transition_time_ms < this->transition_length_ms_)
      {
        ESP_LOGW("CandleLightEffect", "Oops...the transition time is %dms, clamping to %dms",
          transition_time_ms, this->transition_length_ms_);
          transition_time_ms = this->transition_length_ms_;
      }

      //   ESP_LOGD("CandleLightEffect", "Brightness: %.3f    Transition Time: %dms    Short Flickers Left: %d",
      //     new_brightness, transition_time_ms, flickers_left_);
      auto call = this->state_->make_call();
      call.set_color_mode(this->color_mode_);
      call.set_transition_length(transition_time_ms);
      call.set_brightness(new_brightness);

      call.perform();
    }
  protected:
  void set_min_max_brightness()
  {

  }
};

/*
 * A fireplace is glowing with embers with occasional flames brightening the room.
 * Basically the opposite of a candle.
 */
class FireplaceLightEffect : public FlameLightEffect {
 public:
  explicit FireplaceLightEffect(const std::string &name) : FlameLightEffect(name) {}

  void start() override {
    ESP_LOGW("FireplaceLightEffect", "Setting is_baseline_brightness_dim_ to true");
    this->is_baseline_brightness_dim_ = true;
    FlameLightEffect::start();
  }

  void apply() override {

    FlameLightEffect::apply();
    if (this->state_->is_transformer_active()) {
      // Something is already running.
      return;
    }

    float new_brightness;
    int transition_time_ms;

    if (this->flickers_left_ > 0) {
      this->flickers_left_ -= 1;

      float r = random_float();
      if (r <= 0.5){
        transition_time_ms = this->transition_length_ms_;
      } else {
        transition_time_ms = this->transition_length_ms_ + this->transition_length_jitter_ms_;
      }

      if (is_in_bright_flicker_state_) {
        new_brightness = this->flicker_dim_brightness_;
        is_in_bright_flicker_state_ = false;
      } else {
        new_brightness = this->flicker_bright_brightness_;
        is_in_bright_flicker_state_ = true;
      }
    } else {
      // Fireplace gets brighter.
      float r = random_float();
      float dim_depth;
      // Floating point multiplication is faster than division.
      if (r <= this->flicker_probability_ * 0.10f) {
        dim_depth = 2.0f;
        this->flicker_state_ = 3;
      } else if (r <= this->flicker_probability_ * 0.5f) {
        dim_depth = 1.0f;
        this->flicker_state_ = 2;
      } else if (r <= this->flicker_probability_) {
        dim_depth = 0.0f;
        this->flicker_state_ = 1;
      } else {
        dim_depth = 0.0f;
        this->flicker_state_ = 0;
      }

      if(this->flicker_state_ == 0){
        // No flicker.
        this->flicker_bright_brightness_ = this->baseline_brightness_;
        this->flicker_dim_brightness_ = this->baseline_brightness_;
      } else {
        this->set_flicker_brightness_levels(dim_depth);
      }

      if( this->flicker_bright_brightness_ > 1.0f ){ this->flicker_bright_brightness_ = 1.0f; }
      else if( this->flicker_bright_brightness_ < 0.0f ){ this->flicker_bright_brightness_ = 0.0f; }

      if( this->flicker_dim_brightness_ > 1.0f ){ this->flicker_dim_brightness_ = 1.0f; }
      else if( this->flicker_dim_brightness_ < 0.0f ){ this->flicker_dim_brightness_ = 0.0f; }

      // Determine how many flickers should be done for the new flicker_state.
      r = random_float();
      if (this->flicker_state_ == 3) {
        this->flickers_left_ = 1; // Always only one for the brightest flicker state.
      } else if ( r <= 0.2 ) {
        this->flickers_left_ = 10; // 5 bright 5 dim
      } else if ( r <= 0.6 ) {
        this->flickers_left_ = 8;
      } else if ( r <= 0.7 ) {
        this->flickers_left_ = 4;
      } else {
        this->flickers_left_ = 2;
      }

      int delta = abs(this->flicker_state_ - this->previous_flicker_state_);
      r = random_float();
      if (r <= 0.5){
        transition_time_ms = this->transition_length_ms_ * (delta < 1 ? 1 : delta);
      } else {
        transition_time_ms = (this->transition_length_ms_ + this->transition_length_jitter_ms_) * (delta < 1 ? 1 : delta);
      }

      r = random_float();
      if (r <= 0.5){
        is_in_bright_flicker_state_ = true; // start with the brighter of the two.
        new_brightness = flicker_bright_brightness_;
      } else {
        is_in_bright_flicker_state_ = false; // start with the dimmer of the two.
        new_brightness = flicker_dim_brightness_;
      }

      this->previous_flicker_state_ = this->flicker_state_;

      ESP_LOGD("FireplaceLightEffect", "Swing: %d    Flicker Dim: %.3f    Bright: %.3f  Flicker Count: %d",
        dim_depth, this->flicker_dim_brightness_, this->flicker_bright_brightness_, this->flickers_left_);
    }

    if(transition_time_ms < this->transition_length_ms_)
    {
      ESP_LOGW("FireplaceLightEffect", "Oops...the transition time is %dms, clamping to %dms",
        transition_time_ms, this->transition_length_ms_);
        transition_time_ms = this->transition_length_ms_;
    }

    // ESP_LOGD("FireplaceLightEffect", "Brightness: %.3f    Transition Time: %dms    Short Flickers Left: %d",
    //   new_brightness, transition_time_ms, flickers_left_);
    auto call = this->state_->make_call();
    call.set_color_mode(this->color_mode_);
    call.set_transition_length(transition_time_ms);
    call.set_brightness(new_brightness);

    call.perform();
  }

protected:
  void set_flicker_brightness_levels(float level)
  {
        this->flicker_dim_brightness_ = this->baseline_brightness_ * (1 + (level * this->intensity_ / this->number_levels_));
        this->flicker_bright_brightness_ = this->flicker_dim_brightness_ + (this->flicker_intensity_);
  }

  void set_min_max_brightness()
  {
    this->min_brightness_ = this->baseline_brightness_;
    this->max_brightness_ = this->flicker_dim_brightness_ + (this->flicker_intensity_ * this->initial_brightness_);

    ESP_LOGD("FireplaceLightEffect", "Min Brightness: %.3f    Max Brightness: %.3f",
      this->min_brightness_, this->max_brightness_);
  }

};

}  // namespace light
}  // namespace esphome
